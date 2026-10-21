// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Per-file jobs on lanes, cached process-wide (#150). Preparing a file (lexing, refinement and the directive table:
// pp_prepare_into) is a job of its own, so a group of lanes (lane.c) prepares many files at once into a file cache
// (FileCache, c_preprocessor.c), which the translation units that preprocess with it share; and the jobs run ahead of
// the preprocessor along the files' literal includes, so that the units find their files ready. docs/c-files.md has the
// design and its 9700X measurements.
//
// Interning from several lanes. Each lane refines its files with an intern table of its own, whose first ids are the
// predefined names, as the shared table's are. Then a merge gives the spellings new to the shared table their ids there
// in the order of their first occurrences in the round's files: the order of the files (the order one lane prepares them
// in), then of the tokens. That is the order refining the files one after another into the shared table gives, so the
// ids don't depend on the lane count, and one lane refines straight into the shared table: the merge's scalar twin. The
// merge is the exchange of docs/object-writer-and-linker.md's rule 4 ("Lanes"), in steps with a sync between each:
//   1. each lane looks its round's new spellings up in the shared table (read-only): those there get their ids, and the
//      others are candidates, counted by partition of their hashes (files_find_new);
//   2. each lane places its candidates in the partitions' array, after every partition before theirs and, within it,
//      every lane before it (files_place_candidates);
//   3. each lane takes partitions, and finds each distinct spelling's first occurrence among its candidates: the lowest
//      file, then the lowest lane-local id, since a lane refines its files in order and gives its new spellings ids in
//      token order (files_find_first);
//   4. per file, its first occurrences and their bytes; one lane sums them in file order, which gives each file its
//      first new id and record offset, and makes room in the shared table (intern_bulk_begin);
//   5. each lane adds the first occurrences it found with their ids (intern_bulk_add, every spelling the table had
//      placed again if it grew), and the partitions' owners give every other candidate its spelling's id;
//   6. each lane rewrites its files' identifiers from its ids to the shared table's (files_remap).
// For the ids to follow the file order, a lane takes the round's parts in order (by lane_take's numbers, not by
// lane_parts_by_cost's costliest-first order), so that it refines its files in order.
//
// Running ahead (files_run_ahead), a frontier walk in rounds: round 0 prepares the roots (the units' main files), and
// each later round reads the literal header names in the new files' directive tables (#include "x.h" and <x.h>,
// #include_next and #import; a computed include can't be resolved early), finds the files they name by the
// preprocessor's own search (pp_include_candidate, from the path and search directory each file was reached by), and
// prepares the files it found, until a round finds none. The searches advance in waves: each goes through its
// candidates in order until a path the cache never looked up; the waves' new paths are probed on every lane at once
// (file_cache_probe), then added to the cache on one lane, so that each search ends where the preprocessor's will.
// Speculation stays invisible: the walk reads headers that turn out to sit under an inactive #if, which costs time but
// changes nothing, since it runs no macro, takes no branch and reports nothing; a missing header is a path the cache
// knows has none, which a unit reports only if it reaches the include.
//
// main.c includes this file after c_preprocessor.c; every function is static. Layout: FilesSpelling, FilesLane and
// FilesRun; the merge (files_find_new, files_make_candidates, files_place_candidates, files_find_first,
// files_count_first, files_sum_first, files_add_first, files_give_ids, files_remap); a round's preparation
// (files_prepare); the walk's requests and searches (files_read_requests, files_search_requests, files_advance,
// files_collect_probes, files_add_probes, files_search, files_next_round); files_run_ahead, files_run_initialize and
// files_run_release; and the sources under a path, for the tests' and the benchmark's corpora (files_list_sources).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <immintrin.h>

// The exchange's partitions per lane: enough that each lane's share of the candidates is about even, few enough that
// the counts (lanes by partitions) stay small.
#define BUSTER_FILES_PARTITIONS_PER_LANE 64
// A spelling's first occurrence, in the top bit of FilesSpelling.file.
#define BUSTER_FILES_FIRST (1u << 31)
// Most files a round can have, and items, requests and paths a walk can have.
#define BUSTER_FILES_MAX_FILES (1u << 20)
#define BUSTER_FILES_MAX_ITEMS (1u << 22)
#define BUSTER_FILES_MAX_REQUESTS (1u << 24)
#define BUSTER_FILES_ARENA_BYTES (64ull << 30)

// The phases a lane's time goes to (FilesLane.ticks): preparing its files, the merge, reading the files' include lines,
// the searches (and probing paths), and the work one lane does alone while the others wait.
typedef enum FilesPhase
{
    FILES_PHASE_PREPARE,
    FILES_PHASE_MERGE,
    FILES_PHASE_REQUESTS,
    FILES_PHASE_SEARCH,
    FILES_PHASE_ALONE,
    FILES_PHASE_COUNT,
} FilesPhase;

// What a lane knows of a spelling of its table, by its lane-local id: its hash, the shared table's id the merge gave it
// (0 until then), and the round's file it first occurred in, with BUSTER_FILES_FIRST if the merge took this lane's
// occurrence as the spelling's first.
typedef struct FilesSpelling FilesSpelling;
struct FilesSpelling
{
    uint64_t hash;
    IdentId shared;
    uint32_t file;
};

// A candidate of the exchange: a spelling new to the shared table, by its hash, its first 16 bytes (zero past its end),
// its length, and the lane and lane-local id it is at.
typedef struct FilesCandidate FilesCandidate;
struct FilesCandidate
{
    __m128i first;
    uint64_t hash;
    uint32_t length;
    uint32_t lane;
    IdentId local;
};

// An include line of a file the walk reached, with a literal header name (in a lane's arena), and the item that holds it:
// the includer, or BUSTER_PP_NONE for a root, whose "name" is its path.
typedef struct FilesRequest FilesRequest;
struct FilesRequest
{
    const uint8_t* name;
    uint32_t length;
    uint32_t item;
    bool angled;
    bool next;
};

// A path a search needs probed: its bytes (in a lane's arena) and hash.
typedef struct FilesPath FilesPath;
struct FilesPath
{
    const char* path;
    uint32_t length;
    uint64_t hash;
};

// What a lane keeps through a run: its intern table, what it knows of each of its spellings, and per round the files
// it prepared and its lists for the walk.
typedef struct FilesLane FilesLane;
struct FilesLane
{
    InternTable table;
    FilesSpelling* spellings;
    Arena spelling_arena;
    // The table's count when the round started: the round's new spellings are the ids from it on.
    IdentId round_begin;
    // The round's files this lane prepared, in order, and lane_parts_by_cost's order, which the lane takes no part by
    // (see the top of the file) but gives room to.
    uint32_t* files;
    uint32_t file_count;
    uint32_t* order;
    // Where the lane's next candidate of each partition goes.
    uint64_t* offsets;
    // The walk: the requests the lane read this round (in `request_list`) and the paths its searches need probed this
    // wave (in `path_list`), each list contiguous from its arena's start, and their bytes (in `names`, for the round).
    FilesRequest* requests;
    uint32_t request_count;
    FilesPath* paths;
    uint32_t path_count;
    Arena names;
    Arena request_list;
    Arena path_list;
    // The TSC ticks of each phase, the records it prepared and their tokens, and its system calls.
    uint64_t ticks[FILES_PHASE_COUNT];
    uint64_t prepared;
    uint64_t tokens;
    FileCacheCounts counts;
};

// An item of the walk: a path a file was reached by (its entry in the cache's paths), the search directory the path was
// found in (BUSTER_PP_NONE for the includer's directory, an absolute path or a root), and whether it is a root, whose
// #include_next is an #include, as for a main file.
typedef struct FilesItem FilesItem;
struct FilesItem
{
    uint32_t path;
    uint32_t search;
    bool root;
};

// A search: the requests of one key (the same candidates), by the first of them, and how far it got: the next candidate
// to look at, and once it is done, the path it found (BUSTER_PP_NONE for none) and the directory it was found in.
typedef struct FilesSearch FilesSearch;
struct FilesSearch
{
    const FilesRequest* request;
    uint32_t candidate;
    uint32_t path;
    uint32_t found_in;
    bool done;
};

// A run: what every lane of the group reads and writes, made by files_run_initialize.
typedef struct FilesRun FilesRun;
struct FilesRun
{
    FileCache* cache;
    // The search directories (an initialized preprocessor with the units' options), for the walk.
    const Preprocessor* search;
    // The roots (paths), and whether the walk follows their includes.
    const char* const* roots;
    uint32_t root_count;
    bool follow;
    // The lanes' state, by lane index.
    FilesLane* lanes;
    uint32_t lane_count;
    // The round: its records in order, the prefix sums of their bytes (rule 1's costs) and parts, and per file the
    // lane-local ids its lane gave new spellings ([first, end)), how preparing it went, and (one lane sums them) the
    // shared ids and record bytes before its first occurrences.
    uint32_t* records;
    uint32_t record_count;
    uint64_t* costs;
    uint64_t* boundaries;
    IdentId* file_first;
    IdentId* file_end;
    uint8_t* file_status;
    uint32_t* file_ids;
    uint64_t* file_bytes;
    // The exchange: the candidates' counts by lane and partition, the candidates by partition, and each one's first
    // occurrence (an index of the candidates).
    uint32_t partition_count;
    uint32_t* counts;
    uint64_t* partition_starts;
    FilesCandidate* candidates;
    uint32_t* firsts;
    uint64_t candidate_count;
    InternBulk bulk;
    bool bulk_ok;
    // The walk: its items (with a table of them by path and search directory: an item's index + 1, or 0), the round's
    // new ones ([round_items, item_count)), the records queued or prepared in the run (a byte each), and the round's
    // searches, the request of each and its search, and the wave's paths to probe and what each probe found.
    FilesItem* items;
    uint32_t item_count;
    uint32_t round_items;
    uint32_t* item_slots;
    uint32_t item_slot_mask;
    uint8_t* queued;
    FilesSearch* searches;
    uint32_t search_count;
    const FilesRequest** requests;
    uint32_t* request_search;
    uint32_t request_count;
    uint32_t* search_slots;
    uint32_t search_slot_mask;
    FilesPath* probes;
    FileProbe* found;
    uint32_t probe_count;
    uint32_t* probe_slots;
    uint32_t probe_slot_mask;
    // What the run did.
    uint32_t rounds;
    uint32_t waves;
    uint64_t requests_read;
    uint64_t searches_made;
    uint64_t absent;
    uint64_t candidates_exchanged;
    uint64_t shared_found;
    // Memory: the run's arrays (`arena`, which lasts the run), the round's files and their arrays (`files`, made for
    // each round), and the round's exchange, requests and searches (`round`, reset each round).
    Arena arena;
    Arena files;
    Arena round;
    bool failed;
};

// ---------------------------------------------------------------------------------------------------------------------
// The merge

// The run fails if `failed` (memory ran out): any lane may say so at any time, and the lanes read it after a sync.
BUSTER_INLINE void files_fail(FilesRun* run, bool failed)
{
    if (failed)
    {
        __atomic_store_n(&run->failed, true, __ATOMIC_RELAXED);
    }
}

// The partition of a hash, among `partitions`: its high 32 bits scaled, so that the slots of a partition's table (and the
// shared table's buckets), which take the low bits, stay independent of it.
BUSTER_INLINE uint32_t files_partition(uint64_t hash, uint32_t partitions)
{
    return (uint32_t)(((hash >> 32) * partitions) >> 32);
}

// Step 1: this lane's new spellings of the round (ids from round_begin on) are looked up in the shared table: the ones
// it has get its ids, and the others are candidates, counted by partition.
static void files_find_new(FilesRun* run, FilesLane* mine, uint32_t lane)
{
    const InternTable* shared = run->cache->table;
    uint32_t* counts = run->counts + (uint64_t)lane * run->partition_count;
    uint64_t found = 0;
    memset(counts, 0, run->partition_count * sizeof(uint32_t));
    for (IdentId id = mine->round_begin; id < mine->table.count; id += 1)
    {
        const uint8_t* bytes = intern_bytes(&mine->table, id);
        const uint32_t length = intern_length(&mine->table, id);
        FilesSpelling* spelling = &mine->spellings[id];
        spelling->hash = intern_hash_bytes(bytes, length);
        spelling->shared = intern_find_hashed(shared, bytes, length, spelling->hash);
        counts[files_partition(spelling->hash, run->partition_count)] += spelling->shared == 0;
        found += spelling->shared != 0;
    }
    __atomic_add_fetch(&run->shared_found, found, __ATOMIC_RELAXED);
}

// Step 2 on one lane: the candidates' array, for every lane's count of them.
static void files_make_candidates(FilesRun* run)
{
    uint64_t total = 0;
    for (uint32_t p = 0; p < run->partition_count; p += 1)
    {
        run->partition_starts[p] = total;
        for (uint32_t l = 0; l < run->lane_count; l += 1)
        {
            total += run->counts[(uint64_t)l * run->partition_count + p];
        }
    }
    run->partition_starts[run->partition_count] = total;
    run->candidate_count = total;
    run->candidates_exchanged += total;
    run->candidates = arena_push_array(&run->round, total + 1, sizeof(FilesCandidate), 64);
    run->firsts = arena_push_array(&run->round, total + 1, sizeof(uint32_t), 64);
    run->candidates = run->firsts != 0 ? run->candidates : 0;
    files_fail(run, run->candidates == 0);
}

// Step 2: this lane's candidates, in id order, after every partition's before theirs and every lane's before this one
// in the same partition.
static void files_place_candidates(FilesRun* run, FilesLane* mine, uint32_t lane)
{
    const uint32_t partitions = run->partition_count;
    for (uint32_t p = 0; p < partitions; p += 1)
    {
        uint64_t earlier = 0;
        for (uint32_t l = 0; l < lane; l += 1)
        {
            earlier += run->counts[(uint64_t)l * partitions + p];
        }
        mine->offsets[p] = run->partition_starts[p] + earlier;
    }
    for (IdentId id = mine->round_begin; id < mine->table.count; id += 1)
    {
        const FilesSpelling* spelling = &mine->spellings[id];
        if (spelling->shared == 0)
        {
            const uint32_t length = intern_length(&mine->table, id);
            const uint64_t at = mine->offsets[files_partition(spelling->hash, partitions)];
            mine->offsets[files_partition(spelling->hash, partitions)] = at + 1;
            run->candidates[at] = (FilesCandidate){
                .first = intern_first_bytes(intern_bytes(&mine->table, id), length),
                .hash = spelling->hash,
                .length = length,
                .lane = lane,
                .local = id,
            };
        }
    }
}

// The first candidate of partition `p`, and the one past its last.
static void files_partition_range(const FilesRun* run, uint32_t p, uint64_t* begin, uint64_t* end)
{
    *begin = run->partition_starts[p];
    *end = run->partition_starts[p + 1];
}

// Are two candidates the same spelling? Their hashes, lengths and first 16 bytes, and past 16 bytes their records.
static bool files_same_spelling(const FilesRun* run, const FilesCandidate* a, const FilesCandidate* b)
{
    bool result = a->hash == b->hash && a->length == b->length && _mm_movemask_epi8(_mm_cmpeq_epi8(a->first, b->first)) == 0xFFFF;
    if (result && a->length > 16)
    {
        const uint8_t* x = intern_bytes(&run->lanes[a->lane].table, a->local);
        const uint8_t* y = intern_bytes(&run->lanes[b->lane].table, b->local);
        result = memcmp(x + 16, y + 16, a->length - 16) == 0;
    }
    return result;
}

// The order of a candidate's occurrence: its file, then its lane-local id.
BUSTER_INLINE uint64_t files_key(const FilesRun* run, const FilesCandidate* candidate)
{
    const uint32_t file = run->lanes[candidate->lane].spellings[candidate->local].file & ~BUSTER_FILES_FIRST;
    return ((uint64_t)file << 32) | candidate->local;
}

// Step 3, for the partitions this lane takes: each distinct spelling's first occurrence among its candidates, through a
// table of the partition's spellings (by the low bits of their hashes) that keeps each one's first so far. Every
// candidate gets its spelling's first in `firsts`, and the first is marked in its lane's spellings.
static void files_find_first(FilesRun* run, uint32_t p, Arena* scratch)
{
    uint64_t begin;
    uint64_t end;
    files_partition_range(run, p, &begin, &end);
    uint32_t slots = 16;
    while (slots < 2 * (end - begin))
    {
        slots *= 2;
    }
    const uint64_t mark = scratch->length;
    uint32_t* table = arena_push_array(scratch, slots, sizeof(uint32_t), 64);
    files_fail(run, table == 0);
    if (table != 0)
    {
        memset(table, 0, slots * sizeof(uint32_t));
        for (uint64_t c = begin; c < end; c += 1)
        {
            const FilesCandidate* candidate = &run->candidates[c];
            uint32_t slot = (uint32_t)candidate->hash & (slots - 1);
            bool placed = false;
            while (!placed)
            {
                const uint32_t entry = table[slot];
                if (entry == 0)
                {
                    table[slot] = (uint32_t)(c - begin) + 1;
                    placed = true;
                }
                else if (files_same_spelling(run, &run->candidates[begin + entry - 1], candidate))
                {
                    if (files_key(run, candidate) < files_key(run, &run->candidates[begin + entry - 1]))
                    {
                        table[slot] = (uint32_t)(c - begin) + 1;
                    }
                    placed = true;
                }
                else
                {
                    slot = (slot + 1) & (slots - 1);
                }
            }
            // The slot, until every candidate is in the table.
            run->firsts[c] = slot;
        }
        for (uint64_t c = begin; c < end; c += 1)
        {
            const uint64_t first = begin + table[run->firsts[c]] - 1;
            run->firsts[c] = (uint32_t)first;
            if (first == c)
            {
                const FilesCandidate* candidate = &run->candidates[c];
                run->lanes[candidate->lane].spellings[candidate->local].file |= BUSTER_FILES_FIRST;
            }
        }
    }
    arena_pop_to(scratch, mark);
}

// Step 4, per file this lane prepared: its first occurrences, and their bytes (each length + 1).
static void files_count_first(FilesRun* run, FilesLane* mine)
{
    for (uint32_t f = 0; f < mine->file_count; f += 1)
    {
        const uint32_t file = mine->files[f];
        uint32_t ids = 0;
        uint64_t bytes = 0;
        for (IdentId id = run->file_first[file]; id < run->file_end[file]; id += 1)
        {
            const bool first = (mine->spellings[id].file & BUSTER_FILES_FIRST) != 0;
            ids += first;
            bytes += first ? (uint64_t)intern_length(&mine->table, id) + 1 : 0;
        }
        run->file_ids[file] = ids;
        run->file_bytes[file] = bytes;
    }
}

// Step 4 on one lane: the files' counts summed in file order (each file's first id and record offset, from the room the
// shared table makes for them all).
static void files_sum_first(FilesRun* run)
{
    uint32_t ids = 0;
    uint64_t bytes = 0;
    for (uint32_t file = 0; file < run->record_count; file += 1)
    {
        const uint32_t file_ids = run->file_ids[file];
        const uint64_t file_bytes = run->file_bytes[file];
        run->file_ids[file] = ids;
        run->file_bytes[file] = bytes;
        ids += file_ids;
        bytes += file_bytes;
    }
    run->bulk_ok = intern_bulk_begin(run->cache->table, ids, bytes, &run->bulk);
}

// Step 5: this lane's first occurrences, file by file in order, with their ids and records.
static void files_add_first(FilesRun* run, FilesLane* mine)
{
    InternTable* shared = run->cache->table;
    for (uint32_t f = 0; f < mine->file_count; f += 1)
    {
        const uint32_t file = mine->files[f];
        IdentId next = run->bulk.first + run->file_ids[file];
        uint64_t offset = run->bulk.records + run->file_bytes[file];
        for (IdentId id = run->file_first[file]; id < run->file_end[file]; id += 1)
        {
            FilesSpelling* spelling = &mine->spellings[id];
            if ((spelling->file & BUSTER_FILES_FIRST) != 0)
            {
                const uint32_t length = intern_length(&mine->table, id);
                intern_bulk_add(shared, next, intern_bytes(&mine->table, id), length, (uint32_t)offset, spelling->hash);
                spelling->shared = next;
                next += 1;
                offset += (uint64_t)length + 1;
            }
        }
    }
}

// Step 5 for the partitions this lane takes: every candidate gets its spelling's id, from its first occurrence.
static void files_give_ids(FilesRun* run, uint32_t p)
{
    uint64_t begin;
    uint64_t end;
    files_partition_range(run, p, &begin, &end);
    for (uint64_t c = begin; c < end; c += 1)
    {
        const FilesCandidate* candidate = &run->candidates[c];
        const FilesCandidate* first = &run->candidates[run->firsts[c]];
        run->lanes[candidate->lane].spellings[candidate->local].shared = run->lanes[first->lane].spellings[first->local].shared;
    }
}

// Step 6: a prepared file's identifiers (and its guard) from the lane's ids to the shared table's, 64 tokens at a time:
// one compare finds the identifiers, and those that aren't predefined names (whose ids every table shares) take their
// shared ids.
static void files_remap(PPFileData* data, const FilesSpelling* spellings)
{
    for (uint32_t i = 0; i < data->token_count; i += 64)
    {
        const __mmask64 valid = _bzhi_u64(~0ull, data->token_count - i < 64 ? data->token_count - i : 64);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, data->kinds + i);
        uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F)), _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        for (; identifiers != 0; identifiers &= identifiers - 1)
        {
            const uint32_t t = i + (uint32_t)__builtin_ctzll(identifiers);
            const IdentId id = data->values[t];
            data->values[t] = id >= C_NAME_COUNT ? spellings[id].shared : id;
        }
    }
    data->guard = data->guard >= C_NAME_COUNT ? spellings[data->guard].shared : data->guard;
}

// ---------------------------------------------------------------------------------------------------------------------
// A round's preparation

// The TSC ticks since `*tick` go to this lane's phase, and `*tick` becomes now.
BUSTER_INLINE void files_tick(FilesLane* mine, FilesPhase phase, uint64_t* tick)
{
    const uint64_t now = __rdtsc();
    mine->ticks[phase] += now - *tick;
    *tick = now;
}

// Prepares the round's records (run->records, none prepared yet, in order) on every lane of the group, which all call it:
// with one lane, one after another straight into the shared table; with more, rule 1's parts of them by their bytes,
// each lane into its own table, then the merge.
static void files_prepare(FilesRun* run)
{
    const uint32_t lane = lane_index();
    FilesLane* mine = &run->lanes[lane];
    FileCache* cache = run->cache;
    uint64_t tick = __rdtsc();
    if (run->lane_count == 1)
    {
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        for (uint32_t file = 0; file < run->record_count; file += 1)
        {
            PPFileData* data = &cache->records[run->records[file]];
            run->file_status[file] = (uint8_t)pp_prepare_into(&cache->prepare, cache->table, arenas->columns, &arenas->arena, data, 0);
            mine->prepared += run->file_status[file] == PP_PREPARE_DONE;
            mine->tokens += run->file_status[file] == PP_PREPARE_DONE ? data->token_count : 0;
        }
        files_tick(mine, FILES_PHASE_PREPARE, &tick);
    }
    else
    {
        mine->round_begin = mine->table.count;
        mine->file_count = 0;
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        const uint64_t parts = lane_parts_by_cost(run->costs, run->record_count, run->boundaries, mine->order);
        for (uint64_t part = lane_take(parts); part < parts; part = lane_take(parts))
        {
            for (uint64_t file = run->boundaries[part]; file < run->boundaries[part + 1]; file += 1)
            {
                PPFileData* data = &cache->records[run->records[file]];
                const IdentId first = mine->table.count;
                run->file_status[file] = (uint8_t)pp_prepare_into(&cache->prepare, &mine->table, arenas->columns, &arenas->arena, data, 0);
                run->file_first[file] = first;
                run->file_end[file] = mine->table.count;
                for (IdentId id = first; id < mine->table.count; id += 1)
                {
                    mine->spellings[id].file = (uint32_t)file;
                }
                mine->files[mine->file_count] = (uint32_t)file;
                mine->file_count += 1;
            }
        }
        files_tick(mine, FILES_PHASE_PREPARE, &tick);
        lane_sync();
        files_find_new(run, mine, lane);
        lane_sync();
        if (lane == 0)
        {
            files_make_candidates(run);
        }
        // A broadcast's value is lane 0's: the others don't read what lane 0 is writing.
        const bool placed = lane_broadcast(lane == 0 && run->candidates != 0, 0) != 0;
        if (placed)
        {
            files_place_candidates(run, mine, lane);
        }
        lane_sync();
        const Range64 partitions = lane_range(run->partition_count);
        Scratch scratch = scratch_begin(0);
        for (uint64_t p = partitions.begin; placed && p < partitions.end; p += 1)
        {
            files_find_first(run, (uint32_t)p, scratch.arena);
        }
        scratch_end(scratch);
        lane_sync();
        files_count_first(run, mine);
        files_tick(mine, FILES_PHASE_MERGE, &tick);
        lane_sync();
        if (lane == 0 && placed)
        {
            files_sum_first(run);
            files_tick(mine, FILES_PHASE_ALONE, &tick);
        }
        const bool room = lane_broadcast(lane == 0 && placed && run->bulk_ok, 0) != 0;
        if (room)
        {
            const Range64 old = lane_range(run->bulk.first);
            intern_bulk_replace(cache->table, &run->bulk, (uint32_t)old.begin, (uint32_t)old.end);
            files_add_first(run, mine);
        }
        lane_sync();
        for (uint64_t p = partitions.begin; room && p < partitions.end; p += 1)
        {
            files_give_ids(run, (uint32_t)p);
        }
        lane_sync();
        if (lane == 0 && room)
        {
            intern_bulk_end(cache->table, &run->bulk);
        }
        // A file whose spellings got no shared ids stays unprepared: a unit that enters it prepares it again, and says why
        // it can't.
        for (uint32_t f = 0; f < mine->file_count; f += 1)
        {
            const uint32_t file = mine->files[f];
            PPFileData* data = &cache->records[run->records[file]];
            if (run->file_status[file] == PP_PREPARE_DONE && room)
            {
                files_remap(data, mine->spellings);
                mine->prepared += 1;
                mine->tokens += data->token_count;
            }
            else
            {
                data->prepared = false;
                run->file_status[file] = room ? run->file_status[file] : (uint8_t)PP_PREPARE_NO_MEMORY;
            }
        }
        files_tick(mine, FILES_PHASE_MERGE, &tick);
        lane_sync();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The walk

// Reads the requests of the round's new items in this lane's range of them: each literal header name of an #include,
// #include_next or #import in the item's file (a root's request is its path), into the lane's lists.
static void files_read_requests(FilesRun* run, FilesLane* mine)
{
    FileCache* cache = run->cache;
    const Range64 items = lane_range(run->item_count - run->round_items);
    arena_pop_to(&mine->request_list, 0);
    mine->request_count = 0;
    mine->requests = (FilesRequest*)mine->request_list.base;
    for (uint64_t i = run->round_items + items.begin; i < run->round_items + items.end; i += 1)
    {
        const FilesItem* item = &run->items[i];
        const uint32_t record = cache->paths[item->path].record;
        const PPFileData* data = record < cache->record_count ? &cache->records[record] : 0;
        for (uint32_t d = 0; run->follow && data != 0 && data->prepared && d < data->directive_count; d += 1)
        {
            const uint8_t kind = data->directive_kind[d];
            if (kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT)
            {
                uint8_t name[PATH_MAX];
                uint32_t length = 0;
                bool angled = false;
                const PPHeaderForm form = pp_literal_header_name(data, d, cache->prepare.refine_flags, name, sizeof(name), &length, &angled, &mine->names);
                uint8_t* bytes = form == PP_HEADER_LITERAL ? arena_push(&mine->names, length + 1, 1) : 0;
                FilesRequest* request = bytes != 0 ? arena_push_array(&mine->request_list, 1, sizeof(FilesRequest), _Alignof(FilesRequest)) : 0;
                files_fail(run, form == PP_HEADER_LITERAL && request == 0);
                if (request != 0)
                {
                    memcpy(bytes, name, length);
                    bytes[length] = 0;
                    *request = (FilesRequest){
                        .name = bytes,
                        .length = length,
                        .item = (uint32_t)i,
                        .angled = angled,
                        .next = kind == PP_DIRECTIVE_INCLUDE_NEXT && !item->root,
                    };
                    mine->request_count += 1;
                }
            }
        }
    }
}

// The includer a request's search starts from: its item's path, as pp_include_candidate takes it.
static PPIncluder files_includer(const FilesRun* run, const FilesRequest* request)
{
    PPIncluder result = { 0 };
    if (request->item != BUSTER_PP_NONE)
    {
        const FileCachePath* path = &run->cache->paths[run->items[request->item].path];
        uint32_t directory_length = BUSTER_PP_NONE;
        for (uint32_t i = 0; i < path->length; i += 1)
        {
            directory_length = path->path[i] == '/' ? i : directory_length;
        }
        result = (PPIncluder){ .path = path->path, .directory_length = directory_length, .system = BUSTER_PP_USER_FILE, .present = true };
    }
    return result;
}

// The key of a request's search, which decides its candidates: its first search directory (or none for an absolute
// name or a root), the includer's directory when the name is looked for there first, and the name, into `key`
// (PATH_MAX + 64 bytes). Returns its length.
static uint32_t files_search_key(const FilesRun* run, const FilesRequest* request, uint8_t* key)
{
    const PPIncluder includer = files_includer(run, request);
    const uint32_t search = request->item != BUSTER_PP_NONE ? run->items[request->item].search : BUSTER_PP_NONE;
    const bool absolute = request->item == BUSTER_PP_NONE || (request->length != 0 && request->name[0] == '/');
    const bool resumes = request->next && search != BUSTER_PP_NONE;
    const bool local = !absolute && !resumes && !request->angled;
    const uint32_t start = absolute ? UINT32_MAX : resumes ? search + 1 : request->angled ? run->search->angled_first : 0;
    uint32_t length = 0;
    memcpy(key, &start, sizeof(start));
    length += sizeof(start);
    key[length] = (uint8_t)(request->item == BUSTER_PP_NONE ? 2 : local);
    length += 1;
    if (local)
    {
        const char* directory = includer.directory_length != BUSTER_PP_NONE ? includer.path : ".";
        const uint32_t directory_length = includer.directory_length != BUSTER_PP_NONE ? includer.directory_length : 1;
        const uint32_t kept = directory_length < PATH_MAX ? directory_length : PATH_MAX;
        memcpy(key + length, directory, kept);
        length += kept;
        key[length] = 0;
        length += 1;
    }
    const uint32_t name_length = request->length < PATH_MAX ? request->length : PATH_MAX;
    memcpy(key + length, request->name, name_length);
    length += name_length;
    return length;
}

// On one lane: the lanes' requests in order (lane by lane, each in its items' order, so the walk's order), each with the
// search of its key, new keys getting new searches.
static void files_search_requests(FilesRun* run)
{
    Arena* round = &run->round;
    uint32_t total = 0;
    for (uint32_t l = 0; l < run->lane_count; l += 1)
    {
        total += run->lanes[l].request_count;
    }
    run->requests = arena_push_array(round, (uint64_t)total + 1, sizeof(FilesRequest*), 8);
    run->request_search = arena_push_array(round, (uint64_t)total + 1, sizeof(uint32_t), 8);
    run->searches = arena_push_array(round, (uint64_t)total + 1, sizeof(FilesSearch), 8);
    uint32_t slots = 16;
    while (slots < 2 * total)
    {
        slots *= 2;
    }
    run->search_slots = arena_push_array(round, slots, sizeof(uint32_t), 64);
    run->search_slot_mask = slots - 1;
    uint64_t* hashes = arena_push_array(round, (uint64_t)total + 1, sizeof(uint64_t), 8);
    uint8_t* keys = arena_push(round, 2 * (PATH_MAX + 64), 1);
    const bool room = run->requests != 0 && run->request_search != 0 && run->searches != 0 && run->search_slots != 0 && hashes != 0 && keys != 0;
    files_fail(run, !room);
    run->request_count = 0;
    run->search_count = 0;
    if (room)
    {
        memset(run->search_slots, 0, slots * sizeof(uint32_t));
    }
    for (uint32_t l = 0; room && l < run->lane_count; l += 1)
    {
        const FilesLane* lane = &run->lanes[l];
        for (uint32_t r = 0; r < lane->request_count; r += 1)
        {
            const FilesRequest* request = &lane->requests[r];
            const uint32_t key_length = files_search_key(run, request, keys);
            const uint64_t hash = intern_hash_bytes(keys, key_length);
            uint32_t slot = (uint32_t)hash & run->search_slot_mask;
            uint32_t search = BUSTER_PP_NONE;
            while (search == BUSTER_PP_NONE && run->search_slots[slot] != 0)
            {
                const uint32_t other = run->search_slots[slot] - 1;
                const uint32_t other_length = hashes[other] == hash ? files_search_key(run, run->searches[other].request, keys + PATH_MAX + 64) : 0;
                search = hashes[other] == hash && other_length == key_length && memcmp(keys, keys + PATH_MAX + 64, key_length) == 0 ? other : BUSTER_PP_NONE;
                slot = (slot + 1) & run->search_slot_mask;
            }
            if (search == BUSTER_PP_NONE)
            {
                // The probe ended at an empty slot, which the new search takes.
                search = run->search_count;
                run->searches[search] = (FilesSearch){ .request = request, .path = BUSTER_PP_NONE, .found_in = BUSTER_PP_NONE };
                hashes[search] = hash;
                run->search_slots[slot] = search + 1;
                run->search_count += 1;
            }
            run->requests[run->request_count] = request;
            run->request_search[run->request_count] = search;
            run->request_count += 1;
        }
    }
    run->requests_read += run->request_count;
    run->searches_made += run->search_count;
}

// Candidate `index` of a search, as pp_include_candidate gives it for its first request: a root's is its path alone.
static PPCandidate files_candidate(const FilesRun* run, const FilesSearch* search, uint32_t index, char* path, uint32_t* path_length, uint32_t* directory)
{
    const FilesRequest* request = search->request;
    PPCandidate result = PP_CANDIDATE_END;
    *directory = BUSTER_PP_NONE;
    if (request->item == BUSTER_PP_NONE)
    {
        if (index == 0 && request->length < PATH_MAX)
        {
            memcpy(path, request->name, request->length);
            *path_length = request->length;
            result = PP_CANDIDATE_PATH;
        }
    }
    else
    {
        const PPIncluder includer = files_includer(run, request);
        uint8_t system = 0;
        result = pp_include_candidate(run->search, request->name, request->length, request->angled, request->next, &includer, run->items[request->item].search,
                                      index, path, path_length, &system, directory);
    }
    return result;
}

// This lane's range of the round's searches goes through its candidates in order: a path the cache knows answers at
// once (a file there ends the search; none, the next candidate), until the last candidate or a path the cache never
// looked up, which the lane lists for this wave's probes (its bytes in the lane's names).
static void files_advance(FilesRun* run, FilesLane* mine)
{
    FileCache* cache = run->cache;
    const Range64 searches = lane_range(run->search_count);
    arena_pop_to(&mine->path_list, 0);
    mine->paths = (FilesPath*)mine->path_list.base;
    mine->path_count = 0;
    for (uint64_t s = searches.begin; s < searches.end; s += 1)
    {
        FilesSearch* search = &run->searches[s];
        bool waiting = false;
        while (!search->done && !waiting)
        {
            char path[PATH_MAX];
            uint32_t path_length = 0;
            uint32_t directory = BUSTER_PP_NONE;
            const PPCandidate candidate = files_candidate(run, search, search->candidate, path, &path_length, &directory);
            const uint64_t hash = candidate == PP_CANDIDATE_PATH ? intern_hash_bytes((const uint8_t*)path, path_length) : 0;
            const FileCachePath* entry = candidate == PP_CANDIDATE_PATH ? file_cache_find_path(cache, path, path_length, hash) : 0;
            if (candidate == PP_CANDIDATE_END || __atomic_load_n(&run->failed, __ATOMIC_RELAXED))
            {
                search->done = true;
            }
            else if (candidate == PP_CANDIDATE_PATH && entry == 0)
            {
                char* copy = arena_push(&mine->names, (uint64_t)path_length + 1, 1);
                FilesPath* probe = copy != 0 ? arena_push_array(&mine->path_list, 1, sizeof(FilesPath), _Alignof(FilesPath)) : 0;
                files_fail(run, probe == 0);
                if (probe != 0)
                {
                    memcpy(copy, path, path_length);
                    copy[path_length] = 0;
                    *probe = (FilesPath){ .path = copy, .length = path_length, .hash = hash };
                    mine->path_count += 1;
                    waiting = true;
                }
            }
            else if (candidate == PP_CANDIDATE_PATH && entry->record != BUSTER_PP_NONE)
            {
                search->done = true;
                search->path = (uint32_t)(entry - cache->paths);
                search->found_in = directory;
            }
            else
            {
                search->candidate += 1;
            }
        }
    }
}

// On one lane: the paths the lanes' searches wait for, each once, in order (lane by lane, each in its searches' order),
// with room for what each probe finds.
static void files_collect_probes(FilesRun* run)
{
    uint32_t total = 0;
    for (uint32_t l = 0; l < run->lane_count; l += 1)
    {
        total += run->lanes[l].path_count;
    }
    uint32_t slots = 16;
    while (slots < 2 * total)
    {
        slots *= 2;
    }
    run->probes = arena_push_array(&run->round, (uint64_t)total + 1, sizeof(FilesPath), 8);
    run->found = arena_push_array(&run->round, (uint64_t)total + 1, sizeof(FileProbe), 8);
    run->probe_slots = arena_push_array(&run->round, slots, sizeof(uint32_t), 64);
    run->probe_slot_mask = slots - 1;
    run->probe_count = 0;
    const bool room = run->probes != 0 && run->found != 0 && run->probe_slots != 0;
    files_fail(run, !room);
    if (room)
    {
        memset(run->probe_slots, 0, slots * sizeof(uint32_t));
    }
    for (uint32_t l = 0; room && l < run->lane_count; l += 1)
    {
        const FilesLane* lane = &run->lanes[l];
        for (uint32_t p = 0; p < lane->path_count; p += 1)
        {
            const FilesPath* path = &lane->paths[p];
            uint32_t slot = (uint32_t)path->hash & run->probe_slot_mask;
            bool seen = false;
            while (!seen && run->probe_slots[slot] != 0)
            {
                const FilesPath* other = &run->probes[run->probe_slots[slot] - 1];
                seen = other->hash == path->hash && other->length == path->length && memcmp(other->path, path->path, path->length) == 0;
                slot = (slot + 1) & run->probe_slot_mask;
            }
            if (!seen)
            {
                run->probes[run->probe_count] = *path;
                run->probe_slots[slot] = run->probe_count + 1;
                run->probe_count += 1;
            }
        }
    }
}

// On one lane: the wave's probed paths added to the cache, in order, each with its file's record (file_cache_identify).
static void files_add_probes(FilesRun* run)
{
    FileCache* cache = run->cache;
    for (uint32_t p = 0; p < run->probe_count; p += 1)
    {
        const FilesPath* path = &run->probes[p];
        FileCachePath* entry = file_cache_add_path(cache, path->path, path->length, path->hash, &run->found[p]);
        if (entry != 0)
        {
            file_cache_identify(cache, entry);
        }
        else if (run->found[p].mapped)
        {
            munmap((void*)run->found[p].bytes, run->found[p].length);
        }
        files_fail(run, entry == 0);
    }
}

// The round's searches, on every lane, in waves: they advance (files_advance); one lane collects the paths they wait
// for; every lane probes a range of them (file_cache_probe, which only reads the cache); and one lane adds them to the
// cache. Until a wave has no path to probe, when every search is done.
static void files_search(FilesRun* run)
{
    const uint32_t lane = lane_index();
    FilesLane* mine = &run->lanes[lane];
    uint64_t tick = __rdtsc();
    uint32_t probes = 1;
    while (probes != 0)
    {
        files_advance(run, mine);
        files_tick(mine, FILES_PHASE_SEARCH, &tick);
        lane_sync();
        if (lane == 0)
        {
            files_collect_probes(run);
            run->waves += 1;
            files_tick(mine, FILES_PHASE_ALONE, &tick);
        }
        probes = (uint32_t)lane_broadcast(lane == 0 && !run->failed ? run->probe_count : 0, 0);
        const Range64 range = lane_range(probes);
        for (uint64_t p = range.begin; p < range.end; p += 1)
        {
            file_cache_probe(run->cache, run->probes[p].path, &run->found[p], &mine->counts);
        }
        files_tick(mine, FILES_PHASE_SEARCH, &tick);
        lane_sync();
        if (lane == 0 && probes != 0)
        {
            files_add_probes(run);
            files_tick(mine, FILES_PHASE_ALONE, &tick);
        }
        lane_sync();
    }
}

// The item of a path and search directory: found in the walk's table, or added (a root's if the request had no item).
// Returns its index, or BUSTER_PP_NONE if the walk has as many items as it can hold; `*added` says whether it is new.
static uint32_t files_item(FilesRun* run, uint32_t path, uint32_t search, bool root, bool* added)
{
    const uint64_t hash = file_cache_identity_hash(path, search);
    uint32_t slot = (uint32_t)hash & run->item_slot_mask;
    uint32_t result = BUSTER_PP_NONE;
    *added = false;
    while (result == BUSTER_PP_NONE && run->item_slots[slot] != 0)
    {
        const FilesItem* item = &run->items[run->item_slots[slot] - 1];
        result = item->path == path && item->search == search ? run->item_slots[slot] - 1 : BUSTER_PP_NONE;
        slot = (slot + 1) & run->item_slot_mask;
    }
    if (result == BUSTER_PP_NONE && run->item_count < BUSTER_FILES_MAX_ITEMS)
    {
        result = run->item_count;
        run->items[result] = (FilesItem){ .path = path, .search = search, .root = root };
        run->item_count += 1;
        run->item_slots[slot] = result + 1;
        *added = true;
        // The table is never more than half full, and can grow to twice as large as the most items there can be.
        if (2 * run->item_count > run->item_slot_mask + 1)
        {
            const uint32_t slots = 2 * (run->item_slot_mask + 1);
            memset(run->item_slots, 0, slots * sizeof(uint32_t));
            run->item_slot_mask = slots - 1;
            for (uint32_t i = 0; i < run->item_count; i += 1)
            {
                uint32_t place = (uint32_t)file_cache_identity_hash(run->items[i].path, run->items[i].search) & run->item_slot_mask;
                while (run->item_slots[place] != 0)
                {
                    place = (place + 1) & run->item_slot_mask;
                }
                run->item_slots[place] = i + 1;
            }
        }
    }
    files_fail(run, result == BUSTER_PP_NONE);
    return result;
}

// On one lane, after the round's searches: the items they found, each once and in the walk's order (the requests'),
// the round's new ones from round_items on, and the next round's records (the new items' files not yet prepared or
// queued), with their costs (rule 1's: the prefix sums of their bytes) and per-file arrays. Then the round's memory is
// given back.
static void files_next_round(FilesRun* run)
{
    FileCache* cache = run->cache;
    run->round_items = run->item_count;
    arena_pop_to(&run->files, 0);
    run->records = (uint32_t*)run->files.base;
    run->record_count = 0;
    for (uint32_t r = 0; !run->failed && r < run->request_count; r += 1)
    {
        const FilesRequest* request = run->requests[r];
        const FilesSearch* search = &run->searches[run->request_search[r]];
        run->absent += search->path == BUSTER_PP_NONE;
        bool added = false;
        const uint32_t item = search->path != BUSTER_PP_NONE ? files_item(run, search->path, search->found_in, request->item == BUSTER_PP_NONE, &added) : BUSTER_PP_NONE;
        const uint32_t record = item != BUSTER_PP_NONE ? cache->paths[search->path].record : BUSTER_PP_NONE;
        if (added && record < cache->record_count && !cache->records[record].prepared && run->queued[record] == 0)
        {
            uint32_t* slot = arena_push_array(&run->files, 1, sizeof(uint32_t), _Alignof(uint32_t));
            files_fail(run, slot == 0);
            if (slot != 0)
            {
                *slot = record;
                run->queued[record] = 1;
                run->record_count += 1;
            }
        }
    }
    const uint64_t count = run->record_count;
    run->costs = arena_push_array(&run->files, count + 1, sizeof(uint64_t), 64);
    run->boundaries = arena_push_array(&run->files, (uint64_t)BUSTER_LANE_MAX_PARTS + 1, sizeof(uint64_t), 64);
    run->file_first = arena_push_array(&run->files, count + 1, sizeof(IdentId), 64);
    run->file_end = arena_push_array(&run->files, count + 1, sizeof(IdentId), 64);
    run->file_status = arena_push_array(&run->files, count + 1, sizeof(uint8_t), 64);
    run->file_ids = arena_push_array(&run->files, count + 1, sizeof(uint32_t), 64);
    run->file_bytes = arena_push_array(&run->files, count + 1, sizeof(uint64_t), 64);
    files_fail(run, run->costs == 0 || run->boundaries == 0 || run->file_first == 0 || run->file_end == 0 || run->file_status == 0 || run->file_ids == 0 ||
                        run->file_bytes == 0);
    uint64_t cost = 0;
    for (uint32_t f = 0; !run->failed && f < count; f += 1)
    {
        cost += cache->records[run->records[f]].length;
        run->costs[f] = cost;
    }
    run->rounds += count != 0;
    arena_pop_to(&run->round, 0);
}

// On one lane: the roots as requests of their own (a root's name is its path), which the first searches look up.
static void files_root_requests(FilesRun* run)
{
    FilesLane* mine = &run->lanes[0];
    arena_pop_to(&mine->request_list, 0);
    mine->requests = (FilesRequest*)mine->request_list.base;
    mine->request_count = 0;
    for (uint32_t r = 0; r < run->root_count; r += 1)
    {
        FilesRequest* request = arena_push_array(&mine->request_list, 1, sizeof(FilesRequest), _Alignof(FilesRequest));
        files_fail(run, request == 0);
        if (request != 0)
        {
            *request = (FilesRequest){ .name = (const uint8_t*)run->roots[r], .length = (uint32_t)strlen(run->roots[r]), .item = BUSTER_PP_NONE };
            mine->request_count += 1;
        }
    }
    for (uint32_t l = 1; l < run->lane_count; l += 1)
    {
        run->lanes[l].request_count = 0;
    }
}

// Every lane's memory for a run, made by the lane itself: its intern table and spellings (with more than one lane), the
// lists of its files, parts and partitions, and the walk's lists. Returns false if any of it can't be reserved.
static bool files_lane_begin(FilesRun* run, FilesLane* mine)
{
    bool result = true;
    if (run->lane_count > 1)
    {
        mine->spelling_arena = arena_reserve((uint64_t)BUSTER_INTERN_MAX_IDS * sizeof(FilesSpelling) + 64);
        mine->spellings = arena_push_array(&mine->spelling_arena, BUSTER_INTERN_MAX_IDS, sizeof(FilesSpelling), 64);
        result = mine->spellings != 0 && intern_initialize(&mine->table);
    }
    mine->names = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    mine->request_list = arena_reserve((uint64_t)BUSTER_FILES_MAX_REQUESTS * sizeof(FilesRequest));
    mine->path_list = arena_reserve((uint64_t)BUSTER_FILES_MAX_REQUESTS * sizeof(FilesPath));
    mine->files = arena_push_array(&mine->names, BUSTER_FILES_MAX_FILES, sizeof(uint32_t), 64);
    mine->order = arena_push_array(&mine->names, BUSTER_LANE_MAX_PARTS, sizeof(uint32_t), 64);
    mine->offsets = arena_push_array(&mine->names, run->partition_count, sizeof(uint64_t), 64);
    // The names start after these lists, which last the run.
    return result && mine->files != 0 && mine->order != 0 && mine->offsets != 0 && mine->request_list.base != 0 && mine->path_list.base != 0;
}

// Runs ahead on every lane of the group, which all call it: the roots are looked up, then prepared in round 0, and while
// the run follows includes, each round's new files' literal includes are searched for and the files found prepared in
// the next round, until a round finds none. Nothing it does reports a diagnostic. run->failed says if memory ran out, in
// which case the files it didn't prepare are left for the units to prepare.
static void files_run_ahead(FilesRun* run)
{
    const uint32_t lane = lane_index();
    FilesLane* mine = &run->lanes[lane];
    files_fail(run, !files_lane_begin(run, mine));
    // The names' arena keeps the lane's lists from its start: each round gives back only what came after them.
    const uint64_t kept = mine->names.length;
    lane_sync();
    const bool ready = !run->failed;
    if (ready && lane == 0)
    {
        files_root_requests(run);
        files_search_requests(run);
    }
    lane_sync();
    if (ready)
    {
        files_search(run);
    }
    if (ready && lane == 0)
    {
        files_next_round(run);
    }
    bool more = lane_broadcast(lane == 0 && ready && !run->failed && run->item_count > run->round_items, 0) != 0;
    while (more)
    {
        if (run->record_count != 0)
        {
            files_prepare(run);
        }
        uint64_t tick = __rdtsc();
        arena_pop_to(&mine->names, kept);
        files_read_requests(run, mine);
        files_tick(mine, FILES_PHASE_REQUESTS, &tick);
        lane_sync();
        if (lane == 0)
        {
            files_search_requests(run);
            files_tick(mine, FILES_PHASE_ALONE, &tick);
        }
        lane_sync();
        files_search(run);
        if (lane == 0)
        {
            files_next_round(run);
        }
        more = lane_broadcast(lane == 0 && !run->failed && run->item_count > run->round_items, 0) != 0;
    }
}

// The entry point lane_run runs on each lane.
static void files_run_lane(void* argument)
{
    files_run_ahead(argument);
}

// A run for `lane_count` lanes, which fills `cache` (whose intern table it refines into) from `roots`, following their
// includes as `search` (a preprocessor initialized with the units' options and this cache) would search for them, if
// `follow`. Returns false if the run's memory can't be reserved. Then lane_run(lane_count, files_run_lane, run, cpus)
// runs it, and files_run_release releases it.
static __attribute__((unused)) bool files_run_initialize(FilesRun* run, FileCache* cache, const Preprocessor* search, const char* const* roots, uint32_t root_count, bool follow,
                                                         uint32_t lane_count)
{
    *run = (FilesRun){
        .cache = cache,
        .search = search,
        .roots = roots,
        .root_count = root_count,
        .follow = follow,
        .lane_count = lane_count,
        .partition_count = BUSTER_FILES_PARTITIONS_PER_LANE * lane_count,
    };
    run->arena = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->files = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->round = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->lanes = arena_push_array(&run->arena, lane_count, sizeof(FilesLane), 64);
    run->counts = arena_push_array(&run->arena, (uint64_t)lane_count * run->partition_count, sizeof(uint32_t), 64);
    run->partition_starts = arena_push_array(&run->arena, (uint64_t)run->partition_count + 1, sizeof(uint64_t), 64);
    run->items = arena_push_array(&run->arena, BUSTER_FILES_MAX_ITEMS, sizeof(FilesItem), 64);
    run->item_slots = arena_push_array(&run->arena, 2ull * BUSTER_FILES_MAX_ITEMS, sizeof(uint32_t), 64);
    run->queued = arena_push(&run->arena, BUSTER_PP_MAX_FILES, 64);
    run->item_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    bool result = lane_count >= 1 && lane_count <= BUSTER_LANE_MAX_COUNT && run->lanes != 0 && run->counts != 0 && run->partition_starts != 0 && run->items != 0 &&
                  run->item_slots != 0 &&
                  run->queued != 0 && run->files.base != 0 && run->round.base != 0;
    if (result)
    {
        memset(run->lanes, 0, lane_count * sizeof(FilesLane));
    }
    return result;
}

// Releases a run's memory (not the cache's), and adds what its lanes prepared to the cache's counts.
static __attribute__((unused)) void files_run_release(FilesRun* run)
{
    for (uint32_t l = 0; run->lanes != 0 && l < run->lane_count; l += 1)
    {
        FilesLane* lane = &run->lanes[l];
        run->cache->prepared_records += lane->prepared;
        run->cache->prepared_tokens += lane->tokens;
        intern_release(&lane->table);
        arena_release(&lane->spelling_arena);
        arena_release(&lane->names);
        arena_release(&lane->request_list);
        arena_release(&lane->path_list);
    }
    arena_release(&run->arena);
    arena_release(&run->files);
    arena_release(&run->round);
    *run = (FilesRun){ 0 };
}

// ---------------------------------------------------------------------------------------------------------------------
// The sources under a path

#define BUSTER_FILES_MAX_DEPTH 64

// A list of paths, in an arena of its own.
typedef struct FilesSources FilesSources;
struct FilesSources
{
    Arena arena;
    const char** paths;
    uint32_t count;
    uint32_t capacity;
};

static int files_compare_paths(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static bool files_add_source(FilesSources* sources, const char* path)
{
    const uint64_t length = strlen(path);
    char* copy = sources->count < sources->capacity ? arena_push(&sources->arena, length + 1, 1) : 0;
    if (copy != 0)
    {
        memcpy(copy, path, length + 1);
        sources->paths[sources->count] = copy;
        sources->count += 1;
    }
    return copy != 0;
}

// Adds `path` to the list if it is a file, or else the .c and .h files under the directory (names that start with '.'
// and symbolic links skipped), in byte order of their paths so that the list doesn't depend on the file system.
// Returns false if the path can't be read or the list is full.
static __attribute__((unused)) bool files_list_sources(FilesSources* sources, const char* path)
{
    if (sources->arena.base == 0)
    {
        sources->arena = arena_reserve(BUSTER_FILES_ARENA_BYTES);
        sources->capacity = BUSTER_FILES_MAX_FILES;
        sources->paths = arena_push_array(&sources->arena, sources->capacity, sizeof(const char*), 64);
    }
    struct stat stats;
    bool result = sources->paths != 0 && stat(path, &stats) == 0;
    const uint32_t first = sources->count;
    if (result && !S_ISDIR(stats.st_mode))
    {
        result = files_add_source(sources, path);
    }
    else if (result)
    {
        DIR* directories[BUSTER_FILES_MAX_DEPTH];
        static char names[BUSTER_FILES_MAX_DEPTH][PATH_MAX];
        uint32_t depth = 0;
        directories[0] = opendir(path);
        if (directories[0] != 0)
        {
            snprintf(names[0], sizeof(names[0]), "%s", path);
            depth = 1;
        }
        while (depth != 0)
        {
            struct dirent* entry = result ? readdir(directories[depth - 1]) : 0;
            if (entry == 0)
            {
                closedir(directories[depth - 1]);
                depth -= 1;
            }
            else if (entry->d_name[0] != '.')
            {
                char child[PATH_MAX];
                snprintf(child, sizeof(child), "%s/%s", names[depth - 1], entry->d_name);
                struct stat child_stats;
                const uint64_t length = strlen(entry->d_name);
                const bool source = length > 2 && entry->d_name[length - 2] == '.' && (entry->d_name[length - 1] == 'c' || entry->d_name[length - 1] == 'h');
                if (lstat(child, &child_stats) == 0 && S_ISDIR(child_stats.st_mode) && depth < BUSTER_FILES_MAX_DEPTH)
                {
                    directories[depth] = opendir(child);
                    if (directories[depth] != 0)
                    {
                        snprintf(names[depth], sizeof(names[depth]), "%s", child);
                        depth += 1;
                    }
                }
                else if (S_ISREG(child_stats.st_mode) && source)
                {
                    result = files_add_source(sources, child);
                }
            }
        }
        qsort(sources->paths + first, sources->count - first, sizeof(const char*), files_compare_paths);
    }
    return result;
}
