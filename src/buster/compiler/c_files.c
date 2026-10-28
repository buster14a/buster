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
//      every lane before it, each with the order of its occurrence: its file, then its lane-local id
//      (files_place_candidates);
//   3. each lane takes partitions, and finds each distinct spelling's first occurrence among its candidates: the lowest
//      file, then the lowest lane-local id, since a lane refines its files in order and gives its new spellings ids in
//      token order (files_find_first);
//   4. each lane reads which of its candidates are first occurrences, and counts them and their bytes per file; one
//      lane sums the counts in file order, which gives each file its first new id and record offset, and makes room in
//      the shared table (intern_bulk_begin);
//   5. each lane adds its first occurrences with their ids (intern_bulk_add, every spelling the table had placed again
//      if it grew), then reads every other candidate's id from its spelling's first (files_take_ids);
//   6. each lane rewrites its files' identifiers from its ids to the shared table's (files_remap).
// A lane writes only its own spellings and its own segments of the partitions, and reads the others' through the
// candidates, so that no two lanes write one line but at a segment's edge (rule 2). For the ids to follow the file
// order, a lane prepares its files in order: it takes the round's parts by lane_take's numbers, not by
// lane_parts_by_cost's costliest-first order. A round is prepared by one lane per BUSTER_FILES_LANE_BYTES of its bytes, up
// to every lane: one of few bytes, or of one file, by lane 0 alone, straight into the shared table, as with one lane,
// which gives the same ids without a merge.
//
// Running ahead (files_run_ahead), a frontier walk in rounds: round 0 prepares the roots (the units' main files), and
// each later round reads the literal header names in the new files' directive tables (#include "x.h" and <x.h>,
// #include_next and #import; a computed include can't be resolved early), finds the files they name by the
// preprocessor's own search (pp_include_candidate, from the path and search directory each file was reached by), and
// prepares the files it found, until a round finds none. The searches advance in waves: each goes through its
// candidates in order until a path the cache never looked up; the waves' new paths are probed on every lane at once
// (file_cache_probe), then added to the cache on one lane, so that each search ends where the preprocessor's will.
// A path that leads to a file the walk has already taken in the same context (the same search directory, and a
// directory that is the same directory by its device and inode, as ./ and sub/.. make) is an alias, whose includes
// aren't read again: a header that includes itself through ./ would otherwise make paths of its own without end.
// Speculation stays invisible: the walk reads headers that turn out to sit under an inactive #if, which costs time but
// changes nothing, since it runs no macro, takes no branch and reports nothing; a missing header is a path the cache
// knows has none, which a unit reports only if it reaches the include; and a probe opens a path without blocking, so
// that a FIFO no unit opens is a path with no file rather than a wait for a writer.
//
// main.c includes this file after c_preprocessor.c; every function is static. Layout: FilesSpelling, FilesLane and
// FilesRun; the merge (files_find_new, files_make_candidates, files_place_candidates, files_find_first,
// files_count_first, files_sum_first, files_add_first, files_take_ids, files_remap); a round's preparation
// (files_prepare); the walk's requests and searches (files_read_requests, files_search_requests, files_advance,
// files_collect_probes, files_add_probes, files_search, files_next_round); files_run_ahead, files_run_initialize and
// files_run_release; and the sources under a path, for the tests' and the benchmark's corpora (files_list_sources).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <immintrin.h>

// The exchange's partitions per lane: enough that each lane's share of the candidates is about even, few enough that a
// lane placing its candidates writes to few places at once.
#define BUSTER_FILES_PARTITIONS_PER_LANE 16
// A spelling's first occurrence, in the top bit of FilesSpelling.file.
#define BUSTER_FILES_FIRST (1u << 31)
// A round of BUSTER_FILES_SHARED_BYTES or more of its files' bytes is prepared by one lane per BUSTER_FILES_LANE_BYTES of
// them, up to every lane of the group, and a smaller one by lane 0 alone. A round of several lanes pays the merge and the
// working lanes' first tables, about 0.5 to 1 ms on the 9700X: sqlite3.c's round of 3.1 MB took 1.60 ms on 2 lanes
// against 1.37 on one, while one lane per MB rather than per 2 MB made this repository's walk 1.2 ms faster on 7 and 8
// lanes (docs/c-files.md, "Where the time goes"). With all the group's lanes, a round of fewer bytes per lane than
// BUSTER_FILES_SPLIT_BYTES is split once (lane_range_by_cost) rather than cut into parts (rule 1): preparing takes about
// 3 cycles a byte, so 128 KB is about the 64 µs a phase needs per lane to pay for taking parts.
#define BUSTER_FILES_SHARED_BYTES (4u << 20)
#define BUSTER_FILES_LANE_BYTES (1u << 20)
#define BUSTER_FILES_SPLIT_BYTES (128u << 10)
// A lane's own table starts with a bucket for every this many bytes of its share of its first round (at least
// BUSTER_FILES_MIN_BUCKETS, and at most BUSTER_FILES_MAX_BUCKETS, the room the lane reserves for them as it starts; past
// them it grows as any table does), about what the C library's files need, so that it rarely grows.
#define BUSTER_FILES_BYTES_PER_BUCKET 320
#define BUSTER_FILES_MIN_BUCKETS 1024
#define BUSTER_FILES_MAX_BUCKETS (1u << 18)
// Shared-table slots a lane prefetches before it takes them (files_add_first), and spellings whose buckets it prefetches
// before it looks them up (files_find_new).
#define BUSTER_FILES_PLACE_BATCH 32
#define BUSTER_FILES_FIND_BATCH 16
// In a group of more than BUSTER_FILES_HUGE_LANES lanes, each lane's spellings, shared ids and own table start with
// BUSTER_FILES_ARRAY_SMALL_BYTES of base pages, and the token columns of the lanes past lane 0 with
// BUSTER_FILES_COLUMN_SMALL_BYTES (FileCache.lane_small_bytes): there, the lanes that clear fresh huge pages at once are
// bound by memory bandwidth, while each lane's share of a walk's rounds fills little more than one. On the 9700X,
// round 1 of this repository's walk lexed in 1.1 to 1.4 ms per lane on 8 lanes with them, against 2.1 to 2.6 with huge
// pages, and the walk took 9.05 ms against 11.11; on 2 and 3 lanes they made it 0.5 to 0.7 ms slower.
#define BUSTER_FILES_HUGE_LANES 4
#define BUSTER_FILES_ARRAY_SMALL_BYTES (1u << 20)
#define BUSTER_FILES_COLUMN_SMALL_BYTES (2u << 20)
// Linux's MADV_POPULATE_READ (5.14), which the C library's headers may not name: the lane that prepares a mapped file
// maps its pages with it before lexing them, on more than one lane, where the probes map without MAP_POPULATE.
#define BUSTER_FILES_MADV_POPULATE_READ 22
// How lanes get files' bytes (FileCacheBytes, file_cache_probe), and the largest file every lane count reads into its
// arena rather than maps: each mapping holds the process's memory map lock, on which lanes that map at once queue (14%
// of the samples of 8 lanes preparing the C library corpus), and its pages fault in as the lexer reads them, while a
// small file's copy costs little. On the 9700X, reading files of up to 16 KB made the C library corpus 8% faster on 8
// lanes and 7% on one, and musl's walk 21% to 26%, against up to 5% slower for this repository's walk on 4 to 8 lanes
// (docs/c-files.md, "Where the time goes").
#define BUSTER_FILES_BYTES FILE_CACHE_MAP
#define BUSTER_FILES_READ_LIMIT (16u << 10)
// Most files a round can have, and items, requests and paths a walk can have.
#define BUSTER_FILES_MAX_FILES (1u << 20)
#define BUSTER_FILES_MAX_ITEMS (1u << 22)
#define BUSTER_FILES_MAX_REQUESTS (1u << 24)
#define BUSTER_FILES_ARENA_BYTES (64ull << 30)

// The phases a lane's time goes to (FilesLane.ticks): preparing its files; the merge's steps, the lookups in the shared
// table (step 1), the exchange (steps 2 to 4), adding to the shared table and taking ids (step 5) and rewriting the
// files' identifiers (step 6); reading the files' include lines; the searches (and probing paths); the work one lane
// does alone while the others wait; and waiting at syncs.
typedef enum FilesPhase
{
    FILES_PHASE_PREPARE,
    FILES_PHASE_LOOKUP,
    FILES_PHASE_EXCHANGE,
    FILES_PHASE_ADD,
    FILES_PHASE_REMAP,
    FILES_PHASE_REQUESTS,
    FILES_PHASE_SEARCH,
    FILES_PHASE_ALONE,
    FILES_PHASE_WAIT,
    FILES_PHASE_COUNT,
} FilesPhase;

// The rounds a trace keeps apart (the last of them counts every later round too), and the lanes it keeps.
#define BUSTER_FILES_TRACE_ROUNDS 16
#define BUSTER_FILES_TRACE_LANES 8

// Where a run's time went, round by round, for benchmark-files --rounds (FilesRun.trace, null otherwise). Each traced lane
// reads its TSC and its phases' ticks at the walk's start, at the top of each round (before the round is prepared and its
// includes are searched for) and at the end; and lane 0 reads the waves and probes so far then, and each round's files,
// bytes and working lanes. So the first interval is the roots' search, and each later one a round: its preparation, then
// the searches for the files of the next. A walk of more rounds than the trace keeps has no snapshot at the top of those
// past the last it keeps, whose interval goes on to the walk's end, the snapshot after it (test-files' rounds check).
typedef struct FilesTrace FilesTrace;
struct FilesTrace
{
    // The rounds traced (at most BUSTER_FILES_TRACE_ROUNDS), whose snapshots are one more than they, and the end's; and
    // the rounds the walk made.
    uint32_t rounds;
    uint32_t walked;
    uint32_t files[BUSTER_FILES_TRACE_ROUNDS];
    uint64_t bytes[BUSTER_FILES_TRACE_ROUNDS];
    uint32_t working[BUSTER_FILES_TRACE_ROUNDS];
    uint64_t waves[BUSTER_FILES_TRACE_ROUNDS + 2];
    uint64_t probes[BUSTER_FILES_TRACE_ROUNDS + 2];
    uint64_t clock[BUSTER_FILES_TRACE_ROUNDS + 2][BUSTER_FILES_TRACE_LANES];
    uint64_t ticks[BUSTER_FILES_TRACE_ROUNDS + 2][BUSTER_FILES_TRACE_LANES][FILES_PHASE_COUNT];
};

// What a lane knows of a spelling of its table, by its lane-local id: its hash, the round's file it first occurred in
// (with BUSTER_FILES_FIRST once the merge took this lane's occurrence as the spelling's first), and its candidate in the
// exchange. The shared table's id the merge gives it is in an array of its own (FilesLane.shared), which the rewrite of
// every identifier reads.
typedef struct FilesSpelling FilesSpelling;
struct FilesSpelling
{
    uint64_t hash;
    uint32_t file;
    uint32_t candidate;
};

// A candidate of the exchange: a spelling new to the shared table, by its first 16 bytes (zero past its end) and its
// hash, and its key: the round's file it first occurred in (bits 36 to 55), its lane-local id (bits 8 to 35) and its lane
// (bits 0 to 7). Comparing keys orders the occurrences by file, then by lane-local id: a file is one lane's, so the lane
// never decides.
typedef struct FilesCandidate FilesCandidate;
struct FilesCandidate
{
    __m128i first;
    uint64_t hash;
    uint64_t key;
};

_Static_assert(BUSTER_LANE_MAX_COUNT <= 256 && BUSTER_INTERN_MAX_IDS <= (1u << 28) && BUSTER_FILES_MAX_FILES <= (1u << 20),
               "a candidate's key holds its lane, its lane-local id and its file");

BUSTER_INLINE uint32_t files_key_lane(uint64_t key)
{
    return (uint32_t)(key & 0xFF);
}

BUSTER_INLINE IdentId files_key_local(uint64_t key)
{
    return (IdentId)((key >> 8) & ((1u << 28) - 1));
}

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

// What a lane keeps through a run: its intern table, what it knows of each of its spellings and their shared ids (0
// until the merge gives them), and per round the files it prepared and its lists for the walk. Each array that grows is
// in an arena of its own, so that one used for a few kilobytes stays in the arena's first pages, which are base pages
// (lane.c): a lane that touched a huge page for each would have the kernel clear 2 MB for each.
typedef struct FilesLane FilesLane;
struct FilesLane
{
    InternTable table;
    FilesSpelling* spellings;
    IdentId* shared;
    Arena spelling_arena;
    Arena shared_arena;
    // The table's count when the round started: the round's new spellings are the ids from it on.
    IdentId round_begin;
    // Where the lane's next candidate of each partition goes, lane_parts_by_cost's order, which the lane takes no part
    // by (see the top of the file) but gives room to, and the round's files this lane prepared, in order: in `lists`,
    // whose files are given back each round.
    uint64_t* offsets;
    uint32_t* order;
    uint32_t* files;
    uint32_t file_count;
    Arena lists;
    // The walk: the requests the lane read this round (in `request_list`) and the paths its searches need probed this
    // wave (in `path_list`), each list contiguous from its arena's start, and their bytes (in `names`, for the round).
    FilesRequest* requests;
    uint32_t request_count;
    FilesPath* paths;
    uint32_t path_count;
    Arena names;
    Arena request_list;
    Arena path_list;
    // The TSC ticks of each phase (and when the current one started), the records it prepared and their tokens, and its
    // system calls.
    uint64_t ticks[FILES_PHASE_COUNT];
    uint64_t tick;
    uint64_t prepared;
    uint64_t tokens;
    FileCacheCounts counts;
};

// An item of the walk: a path a file was reached by (its entry in the cache's paths), the search directory the path was
// found in (BUSTER_PP_NONE for the includer's directory, an absolute path or a root), and whether it is a root, whose
// #include_next is an #include, as for a main file. What its includes resolve to depends on its file, that search
// directory, whether it is a root, and the directory its path is in, as a directory (its device and inode) rather than
// as spelled (./, sub/.., a symbolic link): a path that leads to an item's file in the same context is an alias of
// that item, whose includes the walk doesn't read again (a file that includes itself through ./ would make paths of
// its own without end). The items of a file are a list (`next`), and the identity of an item's directory is read once,
// only for comparing it with another's.
typedef struct FilesItem FilesItem;
struct FilesItem
{
    uint32_t path;
    uint32_t search;
    uint32_t record;
    uint32_t next;
    // The item this one is an alias of, or BUSTER_PP_NONE.
    uint32_t alias;
    // The directory's identity: FILES_DIRECTORY_UNREAD until read, then known or unknown (its stat failed).
    uint8_t directory;
    bool root;
    uint64_t device;
    uint64_t inode;
};

// What an item knows of its directory's identity.
typedef enum FilesDirectory
{
    FILES_DIRECTORY_UNREAD,
    FILES_DIRECTORY_KNOWN,
    FILES_DIRECTORY_UNKNOWN,
} FilesDirectory;

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
    // The fewest bytes of a round that several lanes prepare, a round's bytes per lane that prepares it (0 for every lane,
    // whatever the bytes), and the bytes per lane under which it is split once rather than cut into parts:
    // BUSTER_FILES_SHARED_BYTES, BUSTER_FILES_LANE_BYTES and BUSTER_FILES_SPLIT_BYTES, which the tests change to reach
    // every way of preparing a round.
    uint64_t shared_bytes;
    uint64_t lane_bytes;
    uint64_t split_bytes;
    // How lanes get files' bytes when there is more than one (FileCacheBytes): BUSTER_FILES_BYTES; and the largest file
    // read into a lane's arena rather than mapped, whatever the lane count: BUSTER_FILES_READ_LIMIT.
    FileCacheBytes bytes;
    uint32_t read_limit;
    // The next part a round's working lanes take when not every lane works on it (lane_take's counter is the group's).
    uint64_t taken;
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
    // The exchange: the candidates' counts by lane and partition, the candidates by partition, each one's first
    // occurrence (an index of the candidates), and a first occurrence's shared id.
    uint32_t partition_count;
    uint32_t* counts;
    uint64_t* partition_starts;
    FilesCandidate* candidates;
    uint32_t* firsts;
    IdentId* candidate_ids;
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
    // Each record's first item + 1 (the head of its list), or 0.
    uint32_t* record_items;
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
    // What the run did, and lane 0's TSC when round 0 (the roots') was prepared (for the benchmark), and where its time
    // went round by round, if the benchmark asks (null otherwise).
    uint64_t roots_prepared;
    uint32_t rounds;
    uint32_t waves;
    uint64_t probes_made;
    FilesTrace* trace;
    uint64_t requests_read;
    uint64_t searches_made;
    uint64_t absent;
    uint64_t candidates_exchanged;
    uint64_t shared_found;
    // Memory: the run's arrays (`arena`, which lasts the run, and an arena each for the items, their table, the records'
    // lists of items and the queued records), the round's files and their arrays (`files`, made for each round), and the round's exchange, requests
    // and searches (`round`, reset each round).
    Arena arena;
    Arena item_arena;
    Arena item_slot_arena;
    Arena record_item_arena;
    Arena queued_arena;
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

// This lane's share of the 4 KB pages of [begin, begin + bytes), each touched for writing: an atomic OR of 0 into its first
// byte, which changes nothing, even in a page that holds other data at a range's edge. The lanes touch fresh memory they
// are about to share this way, each a range of its own, followed by a sync: a page that lanes fault at once is cleared
// by one while the others wait, so lanes that write a fresh shared array where it falls would take its faults one
// after another.
static void files_touch(void* begin, uint64_t bytes)
{
    const uintptr_t first = (uintptr_t)begin & ~(uintptr_t)4095;
    const uint64_t pages = bytes != 0 ? ((uintptr_t)begin + bytes - 1 - first) / 4096 + 1 : 0;
    const Range64 mine = lane_range(pages);
    for (uint64_t page = mine.begin; page < mine.end; page += 1)
    {
        uint8_t* byte = (uint8_t*)(page == 0 ? (uintptr_t)begin : first + page * 4096);
        __atomic_fetch_or(byte, 0, __ATOMIC_RELAXED);
    }
}

// Step 1: this lane's new spellings of the round (ids from round_begin on) are looked up in the shared table: the ones
// it has get its ids, and the others are candidates, counted by partition. BUSTER_FILES_FIND_BATCH spellings at a time
// are hashed and their buckets prefetched before any is looked up, so that the batch's misses in the shared table
// overlap rather than each lookup waiting for its own.
static void files_find_new(FilesRun* run, FilesLane* mine, uint32_t lane)
{
    const InternTable* shared = run->cache->table;
    uint32_t* counts = run->counts + (uint64_t)lane * run->partition_count;
    uint64_t found = 0;
    memset(counts, 0, run->partition_count * sizeof(uint32_t));
    for (IdentId batch = mine->round_begin; batch < mine->table.count; batch += BUSTER_FILES_FIND_BATCH)
    {
        const IdentId end = mine->table.count - batch < BUSTER_FILES_FIND_BATCH ? mine->table.count : batch + BUSTER_FILES_FIND_BATCH;
        for (IdentId id = batch; id < end; id += 1)
        {
            FilesSpelling* spelling = &mine->spellings[id];
            spelling->hash = intern_hash_bytes(intern_bytes(&mine->table, id), intern_length(&mine->table, id));
            _mm_prefetch((const char*)(shared->buckets + (spelling->hash & shared->bucket_mask) * BUSTER_INTERN_BUCKET_BYTES), _MM_HINT_T0);
        }
        for (IdentId id = batch; id < end; id += 1)
        {
            const FilesSpelling* spelling = &mine->spellings[id];
            mine->shared[id] = intern_find_hashed(shared, intern_bytes(&mine->table, id), intern_length(&mine->table, id), spelling->hash);
            counts[files_partition(spelling->hash, run->partition_count)] += mine->shared[id] == 0;
            found += mine->shared[id] != 0;
        }
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
    run->candidate_ids = arena_push_array(&run->round, total + 1, sizeof(IdentId), 64);
    run->candidates = run->firsts != 0 && run->candidate_ids != 0 ? run->candidates : 0;
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
        FilesSpelling* spelling = &mine->spellings[id];
        if (mine->shared[id] == 0)
        {
            const uint32_t length = intern_length(&mine->table, id);
            const uint64_t at = mine->offsets[files_partition(spelling->hash, partitions)];
            mine->offsets[files_partition(spelling->hash, partitions)] = at + 1;
            spelling->candidate = (uint32_t)at;
            run->candidates[at] = (FilesCandidate){
                .first = intern_first_bytes(intern_bytes(&mine->table, id), length),
                .hash = spelling->hash,
                .key = ((uint64_t)spelling->file << 36) | ((uint64_t)id << 8) | lane,
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

// Are two candidates the same spelling? Their hashes and first 16 bytes; those bytes hold a spelling of up to 15 bytes
// whole, with its terminating zero (identifiers have no NUL), and a longer one's lengths and records are compared.
static bool files_same_spelling(const FilesRun* run, const FilesCandidate* a, const FilesCandidate* b)
{
    bool result = a->hash == b->hash && _mm_movemask_epi8(_mm_cmpeq_epi8(a->first, b->first)) == 0xFFFF;
    if (result && _mm_movemask_epi8(_mm_cmpeq_epi8(a->first, _mm_setzero_si128())) == 0)
    {
        const InternTable* x = &run->lanes[files_key_lane(a->key)].table;
        const InternTable* y = &run->lanes[files_key_lane(b->key)].table;
        const IdentId i = files_key_local(a->key);
        const IdentId j = files_key_local(b->key);
        result = intern_length(x, i) == intern_length(y, j) && memcmp(intern_bytes(x, i), intern_bytes(y, j), intern_length(x, i)) == 0;
    }
    return result;
}

// Step 3, for the partitions this lane takes: each distinct spelling's first occurrence among its candidates, through a
// table of the partition's spellings (by the low bits of their hashes) that keeps each one's first so far. Every
// candidate gets its spelling's first in `firsts`.
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
                    if (candidate->key < run->candidates[begin + entry - 1].key)
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
            run->firsts[c] = begin + table[run->firsts[c]] - 1;
        }
    }
    arena_pop_to(scratch, mark);
}

// Step 4, per file this lane prepared: which of its candidates are their spellings' first occurrences, and their count
// and bytes (each length + 1).
static void files_count_first(FilesRun* run, FilesLane* mine)
{
    for (uint32_t f = 0; f < mine->file_count; f += 1)
    {
        const uint32_t file = mine->files[f];
        uint32_t ids = 0;
        uint64_t bytes = 0;
        for (IdentId id = run->file_first[file]; id < run->file_end[file]; id += 1)
        {
            FilesSpelling* spelling = &mine->spellings[id];
            const bool first = mine->shared[id] == 0 && run->firsts[spelling->candidate] == spelling->candidate;
            spelling->file |= first ? BUSTER_FILES_FIRST : 0;
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

// Step 5: this lane's first occurrences, file by file in order, with their ids and records, in batches whose buckets in
// the shared table are prefetched for writing first: a slot is taken with a lock cmpxchg, which can't overlap one miss
// with the next. Each first's id also goes to its candidate, from which the others of its spelling take it.
static void files_add_first(FilesRun* run, FilesLane* mine)
{
    InternTable* shared = run->cache->table;
    IdentId batch[BUSTER_FILES_PLACE_BATCH];
    IdentId batch_ids[BUSTER_FILES_PLACE_BATCH];
    uint32_t batch_offsets[BUSTER_FILES_PLACE_BATCH];
    uint32_t batched = 0;
    for (uint32_t f = 0; f < mine->file_count; f += 1)
    {
        const uint32_t file = mine->files[f];
        IdentId next = run->bulk.first + run->file_ids[file];
        uint64_t offset = run->bulk.records + run->file_bytes[file];
        const IdentId end = run->file_end[file];
        for (IdentId id = run->file_first[file]; id <= end; id += 1)
        {
            const bool first = id < end && (mine->spellings[id].file & BUSTER_FILES_FIRST) != 0;
            if (first)
            {
                _mm_prefetch((const char*)(shared->buckets + (mine->spellings[id].hash & shared->bucket_mask) * BUSTER_INTERN_BUCKET_BYTES), _MM_HINT_ET0);
                batch[batched] = id;
                batch_ids[batched] = next;
                batch_offsets[batched] = (uint32_t)offset;
                batched += 1;
                next += 1;
                offset += (uint64_t)intern_length(&mine->table, id) + 1;
            }
            const bool last = f + 1 == mine->file_count && id == end;
            for (uint32_t b = 0; (batched == BUSTER_FILES_PLACE_BATCH || last) && b < batched; b += 1)
            {
                const IdentId local = batch[b];
                const FilesSpelling* spelling = &mine->spellings[local];
                intern_bulk_add(shared, batch_ids[b], intern_bytes(&mine->table, local), intern_length(&mine->table, local), batch_offsets[b], spelling->hash);
                mine->shared[local] = batch_ids[b];
                run->candidate_ids[spelling->candidate] = batch_ids[b];
            }
            batched = batched == BUSTER_FILES_PLACE_BATCH || last ? 0 : batched;
        }
    }
}

// Step 5, after every lane added its first occurrences: this lane's other candidates (the round's spellings it has and
// another lane, or an earlier file of its own, had first) take their spellings' ids from their firsts' candidates.
static void files_take_ids(FilesRun* run, FilesLane* mine)
{
    for (IdentId id = mine->round_begin; id < mine->table.count; id += 1)
    {
        if (mine->shared[id] == 0)
        {
            mine->shared[id] = run->candidate_ids[run->firsts[mine->spellings[id].candidate]];
        }
    }
}

// Step 6: a prepared file's identifiers (and its guard) from the lane's ids to the shared table's, 64 tokens at a time:
// one compare finds the identifiers, and those that aren't predefined names (whose ids every table shares) take their
// shared ids, 16 at a time by a gather. files_remap_scalar is its twin, a token at a time.
static void files_remap(PPFileData* data, const IdentId* shared)
{
    for (uint32_t i = 0; i < data->token_count; i += 64)
    {
        const __mmask64 valid = _bzhi_u64(~0ull, data->token_count - i < 64 ? data->token_count - i : 64);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, data->kinds + i);
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F)), _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        for (uint32_t quarter = 0; quarter < 4; quarter += 1)
        {
            // The quarter's identifiers, which `valid` keeps within the file's tokens.
            const __mmask16 named = (__mmask16)(identifiers >> (16 * quarter));
            uint32_t* values = data->values + i + 16 * quarter;
            const __m512i ids = _mm512_maskz_loadu_epi32(named, values);
            const __mmask16 local = _mm512_mask_cmpge_epu32_mask(named, ids, _mm512_set1_epi32(C_NAME_COUNT));
            _mm512_mask_storeu_epi32(values, local, _mm512_mask_i32gather_epi32(ids, local, ids, shared, sizeof(IdentId)));
        }
    }
    data->guard = data->guard >= C_NAME_COUNT ? shared[data->guard] : data->guard;
}

static void files_remap_scalar(PPFileData* data, const IdentId* shared)
{
    for (uint32_t t = 0; t < data->token_count; t += 1)
    {
        const IdentId id = data->values[t];
        data->values[t] = (data->kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && id >= C_NAME_COUNT ? shared[id] : id;
    }
    data->guard = data->guard >= C_NAME_COUNT ? shared[data->guard] : data->guard;
}

// ---------------------------------------------------------------------------------------------------------------------
// A round's preparation

// The TSC ticks since the lane's current phase started go to `phase`, and a phase starts now.
BUSTER_INLINE void files_tick(FilesLane* mine, FilesPhase phase)
{
    const uint64_t now = __rdtsc();
    mine->ticks[phase] += now - mine->tick;
    mine->tick = now;
}

// A sync of the lanes, after a part of `phase`: the wait goes to FILES_PHASE_WAIT.
static void files_sync(FilesLane* mine, FilesPhase phase)
{
    files_tick(mine, phase);
    lane_sync();
    files_tick(mine, FILES_PHASE_WAIT);
}

// lane_broadcast of lane 0's `value`, timed as files_sync is.
static uint64_t files_broadcast(FilesLane* mine, FilesPhase phase, uint64_t value)
{
    files_tick(mine, phase);
    const uint64_t result = lane_broadcast(value, 0);
    files_tick(mine, FILES_PHASE_WAIT);
    return result;
}

// The lanes that work on the round: lane 0 alone for fewer than run->shared_bytes of its bytes, and otherwise one per
// run->lane_bytes of them, up to all of them, but never more than its files, and at least one.
static uint32_t files_working_lanes(const FilesRun* run)
{
    const uint64_t bytes = run->record_count != 0 ? run->costs[run->record_count - 1] : 0;
    const uint64_t wanted = run->lane_bytes == 0 ? run->lane_count : bytes < run->shared_bytes ? 1 : bytes / run->lane_bytes;
    const uint32_t working = (uint32_t)(wanted < 1 ? 1 : wanted < run->lane_count ? wanted : run->lane_count);
    return working < run->record_count ? working : run->record_count;
}

// The base pages at the start of a lane's spellings, shared ids and own table: BUSTER_FILES_ARRAY_SMALL_BYTES in a group
// of more than BUSTER_FILES_HUGE_LANES lanes, else arena_reserve's.
static uint64_t files_array_small(const FilesRun* run)
{
    return run->lane_count > BUSTER_FILES_HUGE_LANES ? BUSTER_FILES_ARRAY_SMALL_BYTES : BUSTER_ARENA_SMALL_BYTES;
}

// Prepares a file of the round into `table` (the shared one, or the lane's own), after mapping its pages if it is a
// mapping and there is more than one lane: one lane's probes map with MAP_POPULATE (BUSTER_FILES_MADV_POPULATE_READ).
static PPPrepareStatus files_prepare_file(FilesRun* run, InternTable* table, FileCacheArenas* arenas, PPFileData* data)
{
    if (run->lane_count > 1 && data->mapped && data->length != 0)
    {
        madvise((void*)data->bytes, data->length, BUSTER_FILES_MADV_POPULATE_READ);
    }
    return pp_prepare_into(&run->cache->prepare, table, arenas->columns, &arenas->arena, data, 0);
}

// Prepares the round's records (run->records, none prepared yet, in order) on every lane of the group, which all call it.
// The lanes that work on it are files_working_lanes's: one works alone on lane 0, one file after another straight into
// the shared table; several take rule 1's parts of the files by their bytes (or one range each, for few bytes), each
// into its own table, and then merge. Every way gives the same ids, so which one runs is only a matter of speed: a lane
// alone needs no merge.
static void files_prepare(FilesRun* run)
{
    const uint32_t lane = lane_index();
    FilesLane* mine = &run->lanes[lane];
    FileCache* cache = run->cache;
    const uint64_t bytes = run->record_count != 0 ? run->costs[run->record_count - 1] : 0;
    const uint32_t lanes = files_working_lanes(run);
    if (lanes <= 1)
    {
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        for (uint32_t file = 0; lane == 0 && file < run->record_count; file += 1)
        {
            PPFileData* data = &cache->records[run->records[file]];
            run->file_status[file] = (uint8_t)files_prepare_file(run, cache->table, arenas, data);
            mine->prepared += run->file_status[file] == PP_PREPARE_DONE;
            mine->tokens += run->file_status[file] == PP_PREPARE_DONE ? data->token_count : 0;
        }
        files_sync(mine, FILES_PHASE_PREPARE);
    }
    else
    {
        // A working lane's own table is laid out at its first round of several lanes, in the memory the lane reserved as
        // it started (files_lane_begin), the size of a working lane's share of the round's bytes.
        uint64_t buckets = BUSTER_FILES_MIN_BUCKETS;
        while (buckets < BUSTER_FILES_MAX_BUCKETS && buckets * BUSTER_FILES_BYTES_PER_BUCKET < bytes / lanes)
        {
            buckets *= 2;
        }
        if (lane < lanes && mine->table.records == 0)
        {
            files_fail(run, !intern_initialize_in(&mine->table, buckets));
        }
        mine->round_begin = mine->table.count;
        // The files follow the lists that last the run.
        arena_pop_to(&mine->lists, (uint64_t)((uint8_t*)mine->files - mine->lists.base));
        mine->file_count = 0;
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        // Rule 1: parts by cost taken in order, or with few bytes per lane, one range for each working lane (and none for
        // the others).
        const bool all = lanes == run->lane_count;
        const bool split = bytes / lanes < run->split_bytes;
        const Range64 range = !split ? (Range64){ 0 } : lane < lanes ? range_part_by_cost(run->costs, run->record_count, lane, lanes) : (Range64){ 0 };
        uint64_t parts = 1;
        if (!split)
        {
            files_tick(mine, FILES_PHASE_PREPARE);
            parts = lane_parts_by_cost(run->costs, run->record_count, run->boundaries, mine->order);
            files_tick(mine, FILES_PHASE_WAIT);
        }
        // Every lane takes until it runs out (lane_take's counter needs it), and after a failure prepares nothing more;
        // with fewer working lanes than the group's, the working ones take from the run's counter, and the others none.
        const uint64_t none = parts;
        for (uint64_t part = split ? 0 : all ? lane_take(parts) : lane < lanes ? __atomic_fetch_add(&run->taken, 1, __ATOMIC_RELAXED) : none; part < parts;
             part = split ? parts : all ? lane_take(parts) : __atomic_fetch_add(&run->taken, 1, __ATOMIC_RELAXED))
        {
            const bool failed = __atomic_load_n(&run->failed, __ATOMIC_RELAXED);
            const uint64_t first_file = split ? range.begin : run->boundaries[part];
            const uint64_t last_file = failed ? first_file : split ? range.end : run->boundaries[part + 1];
            for (uint64_t file = first_file; file < last_file; file += 1)
            {
                PPFileData* data = &cache->records[run->records[file]];
                const IdentId first = mine->table.count;
                run->file_status[file] = (uint8_t)files_prepare_file(run, &mine->table, arenas, data);
                run->file_first[file] = first;
                run->file_end[file] = mine->table.count;
                for (IdentId id = first; id < mine->table.count; id += 1)
                {
                    mine->spellings[id].file = (uint32_t)file;
                }
                uint32_t* slot = arena_push_array(&mine->lists, 1, sizeof(uint32_t), _Alignof(uint32_t));
                files_fail(run, slot == 0);
                if (slot != 0)
                {
                    *slot = (uint32_t)file;
                    mine->file_count += 1;
                }
                else
                {
                    // Unlisted, its values are never rewritten to shared ids: a unit that enters it prepares it again.
                    data->prepared = false;
                    run->file_status[file] = (uint8_t)PP_PREPARE_NO_MEMORY;
                }
            }
        }
        files_sync(mine, FILES_PHASE_PREPARE);
        files_find_new(run, mine, lane);
        files_sync(mine, FILES_PHASE_LOOKUP);
        if (lane == 0)
        {
            files_make_candidates(run);
            files_tick(mine, FILES_PHASE_ALONE);
        }
        // A broadcast's value is lane 0's: the others don't read what lane 0 is writing.
        const bool placed = files_broadcast(mine, FILES_PHASE_EXCHANGE, lane == 0 && run->candidates != 0) != 0;
        if (placed)
        {
            files_touch(run->candidates, run->candidate_count * sizeof(FilesCandidate));
            files_touch(run->firsts, run->candidate_count * sizeof(uint32_t));
            files_touch(run->candidate_ids, run->candidate_count * sizeof(IdentId));
        }
        files_sync(mine, FILES_PHASE_EXCHANGE);
        if (placed)
        {
            files_place_candidates(run, mine, lane);
        }
        files_sync(mine, FILES_PHASE_EXCHANGE);
        const Range64 partitions = lane_range(run->partition_count);
        Scratch scratch = scratch_begin(0);
        for (uint64_t p = partitions.begin; placed && p < partitions.end; p += 1)
        {
            files_find_first(run, (uint32_t)p, scratch.arena);
        }
        scratch_end(scratch);
        files_sync(mine, FILES_PHASE_EXCHANGE);
        // Lane 0's word, the same on every lane, on whether every candidate has its first occurrence: there are none
        // without the candidates' arrays, and a lane whose scratch memory ran out (run->failed, now that every lane has
        // passed files_find_first) left its partitions' undecided. Every later step reads them, so without them the
        // round's files stay unprepared, for the units to prepare.
        const bool found = files_broadcast(mine, FILES_PHASE_EXCHANGE, lane == 0 && placed && !__atomic_load_n(&run->failed, __ATOMIC_RELAXED)) != 0;
        if (found)
        {
            files_count_first(run, mine);
        }
        files_sync(mine, FILES_PHASE_EXCHANGE);
        if (lane == 0 && found)
        {
            files_sum_first(run);
            files_tick(mine, FILES_PHASE_ALONE);
        }
        const bool room = files_broadcast(mine, FILES_PHASE_ADD, lane == 0 && found && run->bulk_ok) != 0;
        if (room)
        {
            // The fresh parts of the shared table the lanes write: its buckets and spans if it grew, else the new ids'
            // spans, and the new records.
            InternTable* table = cache->table;
            const uint64_t bucket_bytes = run->bulk.grew ? (table->bucket_mask + 1) * BUSTER_INTERN_BUCKET_BYTES : 0;
            const uint32_t first_span = run->bulk.grew ? 0 : run->bulk.first;
            files_touch(table->buckets, bucket_bytes);
            files_touch(table->spans + first_span, (uint64_t)(run->bulk.first + run->bulk.count - first_span) * sizeof(InternSpan));
            files_touch(table->records + run->bulk.records, run->bulk.bytes);
        }
        files_sync(mine, FILES_PHASE_ADD);
        if (room)
        {
            const Range64 old = lane_range(run->bulk.first);
            intern_bulk_replace(cache->table, &run->bulk, (uint32_t)old.begin, (uint32_t)old.end);
            files_add_first(run, mine);
        }
        files_sync(mine, FILES_PHASE_ADD);
        if (room)
        {
            files_take_ids(run, mine);
        }
        files_sync(mine, FILES_PHASE_ADD);
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
                if (cache->prepare.vectorized)
                {
                    files_remap(data, mine->shared);
                }
                else
                {
                    files_remap_scalar(data, mine->shared);
                }
                mine->prepared += 1;
                mine->tokens += data->token_count;
            }
            else
            {
                data->prepared = false;
                run->file_status[file] = room ? run->file_status[file] : (uint8_t)PP_PREPARE_NO_MEMORY;
            }
        }
        files_sync(mine, FILES_PHASE_REMAP);
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
        // An alias's includes are its item's (see FilesItem).
        for (uint32_t d = 0; run->follow && item->alias == BUSTER_PP_NONE && data != 0 && data->prepared && d < data->directive_count; d += 1)
        {
            const uint8_t kind = data->directive_kind[d];
            if (kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT)
            {
                uint8_t name[PATH_MAX];
                uint32_t length = 0;
                bool angled = false;
                uint32_t stop = 0;
                const PPHeaderForm form = pp_literal_header_name(data, d, cache->prepare.refine_flags, name, sizeof(name), &length, &angled, &stop, &mine->names);
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

// The most bytes files_search_key writes for a request: the start and the kind, the includer's directory (at most its
// item's path, or ".") and a separator, and the name. A header name is shorter than PATH_MAX, but a root's is any
// length, and an includer's directory and a name each may come close to PATH_MAX.
static uint64_t files_search_key_bound(const FilesRun* run, const FilesRequest* request)
{
    const uint64_t directory = request->item != BUSTER_PP_NONE ? (uint64_t)run->cache->paths[run->items[request->item].path].length + 1 : 0;
    return sizeof(uint32_t) + 1 + directory + 1 + request->length;
}

// The key of a request's search, which decides its candidates: its first search directory (or none for an absolute
// name or a root), the includer's directory when the name is looked for there first, and the name, whole, into `key`
// (files_search_key_bound's bytes). Returns its length.
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
        memcpy(key + length, directory, directory_length);
        length += directory_length;
        key[length] = 0;
        length += 1;
    }
    memcpy(key + length, request->name, request->length);
    length += request->length;
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
    // Room for two keys, the request's and one it is compared with, of the round's longest.
    uint64_t bound = 0;
    for (uint32_t l = 0; l < run->lane_count; l += 1)
    {
        for (uint32_t r = 0; r < run->lanes[l].request_count; r += 1)
        {
            const uint64_t request_bound = files_search_key_bound(run, &run->lanes[l].requests[r]);
            bound = request_bound > bound ? request_bound : bound;
        }
    }
    uint8_t* keys = arena_push(round, 2 * bound + 1, 1);
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
                const uint32_t other_length = hashes[other] == hash ? files_search_key(run, run->searches[other].request, keys + bound) : 0;
                search = hashes[other] == hash && other_length == key_length && memcmp(keys, keys + bound, key_length) == 0 ? other : BUSTER_PP_NONE;
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
// looked up, which the lane lists for this wave's probes (its bytes in the lane's names). A place the preprocessor's
// search can't look in (a path too long for one, or one that couldn't be opened for another reason than there being no
// file there) ends the search too, as it ends the preprocessor's with a fatal error (pp_find_include).
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
            else if (candidate == PP_CANDIDATE_SKIP || (entry->probe.error != 0 && !pp_include_error_absent(entry->probe.error)))
            {
                search->done = true;
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
    uint32_t probes = 1;
    while (probes != 0)
    {
        files_advance(run, mine);
        files_sync(mine, FILES_PHASE_SEARCH);
        if (lane == 0)
        {
            files_collect_probes(run);
            run->waves += 1;
            run->probes_made += run->probe_count;
            files_tick(mine, FILES_PHASE_ALONE);
        }
        probes = (uint32_t)files_broadcast(mine, FILES_PHASE_SEARCH, lane == 0 && !run->failed ? run->probe_count : 0);
        // How the lanes get the files' bytes (file_cache_probe): files of up to run->read_limit bytes read, and larger
        // ones mapped, with MAP_POPULATE on one lane, and as run->bytes says on more.
        const Range64 range = lane_range(probes);
        const FileCacheBytes how = run->lane_count > 1 ? run->bytes : FILE_CACHE_MAP_POPULATED;
        for (uint64_t p = range.begin; p < range.end; p += 1)
        {
            file_cache_probe(run->cache, run->probes[p].path, how, run->read_limit, &run->cache->lanes[lane].bytes, &run->found[p], &mine->counts);
        }
        files_sync(mine, FILES_PHASE_SEARCH);
        if (lane == 0 && probes != 0)
        {
            files_add_probes(run);
            files_tick(mine, FILES_PHASE_ALONE);
        }
        files_sync(mine, FILES_PHASE_SEARCH);
    }
}

// The identity of an item's directory (as files_includer takes it from the item's path), read once.
static bool files_item_directory(FilesItem* item, const FileCachePath* path)
{
    if (item->directory == FILES_DIRECTORY_UNREAD)
    {
        uint32_t length = BUSTER_PP_NONE;
        for (uint32_t i = 0; i < path->length; i += 1)
        {
            length = path->path[i] == '/' ? i : length;
        }
        char directory[PATH_MAX];
        const char* name = length == BUSTER_PP_NONE ? "." : length == 0 ? "/" : directory;
        struct stat stats;
        const bool fits = length == BUSTER_PP_NONE || length < sizeof(directory);
        if (fits && length != BUSTER_PP_NONE && length != 0)
        {
            memcpy(directory, path->path, length);
            directory[length] = 0;
        }
        const bool known = fits && stat(name, &stats) == 0;
        item->device = known ? (uint64_t)stats.st_dev : 0;
        item->inode = known ? (uint64_t)stats.st_ino : 0;
        item->directory = known ? FILES_DIRECTORY_KNOWN : FILES_DIRECTORY_UNKNOWN;
    }
    return item->directory == FILES_DIRECTORY_KNOWN;
}

// The item of a path and search directory: found in the walk's table, or added (a root's if the request had no item),
// as an alias of an earlier item of its file whose context is the same (see FilesItem). Returns its index, or
// BUSTER_PP_NONE if the walk has as many items as it can hold; `*added` says whether it is new.
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
        const FileCache* cache = run->cache;
        const uint32_t record = cache->paths[path].record;
        const bool listed = record < cache->record_count;
        result = run->item_count;
        FilesItem* item = &run->items[result];
        *item = (FilesItem){
            .path = path,
            .search = search,
            .record = record,
            .next = listed && run->record_items[record] != 0 ? run->record_items[record] - 1 : BUSTER_PP_NONE,
            .alias = BUSTER_PP_NONE,
            .root = root,
        };
        // An earlier item of the file in the same context, by the identity of their directories (read only when the
        // file has another item of the same search directory and kind).
        for (uint32_t other = item->next; item->alias == BUSTER_PP_NONE && other != BUSTER_PP_NONE; other = run->items[other].next)
        {
            FilesItem* earlier = &run->items[other];
            const bool same = earlier->alias == BUSTER_PP_NONE && earlier->search == search && earlier->root == root &&
                              files_item_directory(earlier, &cache->paths[earlier->path]) && files_item_directory(item, &cache->paths[path]) &&
                              earlier->device == item->device && earlier->inode == item->inode;
            item->alias = same ? other : BUSTER_PP_NONE;
        }
        if (listed)
        {
            run->record_items[record] = result + 1;
        }
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
    run->boundaries = arena_push_array(&run->files, (uint64_t)BUSTER_LANE_PARTS_PER_LANE * run->lane_count + 1, sizeof(uint64_t), 64);
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
    run->taken = 0;
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

// Every lane's memory for a run, made by the lane itself: its intern table's memory and its spellings (with more than one
// lane; files_prepare lays the table out at the lane's first round of several), the lists of its files, parts and
// partitions, and the walk's lists. Returns false if any of it can't be reserved.
static bool files_lane_begin(FilesRun* run, FilesLane* mine)
{
    bool result = true;
    if (run->lane_count > 1)
    {
        uint32_t capacity;
        const uint64_t small = files_array_small(run);
        mine->table.record_arena = arena_reserve_small(intern_layout_bytes(BUSTER_FILES_MAX_BUCKETS, &capacity) + BUSTER_INTERN_RECORD_BYTES_MAX, small);
        mine->spelling_arena = arena_reserve_small((uint64_t)BUSTER_INTERN_MAX_IDS * sizeof(FilesSpelling) + 64, small);
        mine->shared_arena = arena_reserve_small((uint64_t)BUSTER_INTERN_MAX_IDS * sizeof(IdentId) + 64, small);
        result = mine->table.record_arena.base != 0;
        mine->spellings = arena_push_array(&mine->spelling_arena, BUSTER_INTERN_MAX_IDS, sizeof(FilesSpelling), 64);
        mine->shared = arena_push_array(&mine->shared_arena, BUSTER_INTERN_MAX_IDS, sizeof(IdentId), 64);
        result = result && mine->spellings != 0 && mine->shared != 0;
    }
    mine->lists = arena_reserve((uint64_t)BUSTER_FILES_MAX_FILES * sizeof(uint32_t) + (uint64_t)BUSTER_LANE_MAX_PARTS * sizeof(uint32_t) + 4096 * sizeof(uint64_t));
    mine->names = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    mine->request_list = arena_reserve((uint64_t)BUSTER_FILES_MAX_REQUESTS * sizeof(FilesRequest));
    mine->path_list = arena_reserve((uint64_t)BUSTER_FILES_MAX_REQUESTS * sizeof(FilesPath));
    mine->offsets = arena_push_array(&mine->lists, run->partition_count, sizeof(uint64_t), 64);
    mine->order = arena_push_array(&mine->lists, (uint64_t)BUSTER_LANE_PARTS_PER_LANE * run->lane_count, sizeof(uint32_t), 64);
    // The round's files go after these, which last the run.
    mine->files = mine->order != 0 ? (uint32_t*)(mine->lists.base + mine->lists.length) : 0;
    return result && mine->files != 0 && mine->offsets != 0 && mine->names.base != 0 && mine->request_list.base != 0 && mine->path_list.base != 0;
}

// A trace's snapshot `at` (FilesTrace): this lane's TSC at its last phase boundary and its phases' ticks, and on lane 0
// the waves and probes so far and, at the top of a round (`round`), the round's files, bytes and working lanes, those of
// the rounds past the last traced one added to its own. The walk's start is snapshot 0, and its end (not `round`) the one
// after the last traced round's, BUSTER_FILES_TRACE_ROUNDS + 1 at most.
static void files_trace(FilesRun* run, const FilesLane* mine, uint32_t lane, uint32_t at, bool round)
{
    FilesTrace* trace = run->trace;
    if (trace != 0 && lane < BUSTER_FILES_TRACE_LANES)
    {
        const uint32_t slot = round && at > BUSTER_FILES_TRACE_ROUNDS ? BUSTER_FILES_TRACE_ROUNDS : at;
        // A round past the last traced one has no snapshot of its own: the last traced round's goes on to the end.
        if (!round || at <= BUSTER_FILES_TRACE_ROUNDS)
        {
            trace->clock[slot][lane] = mine->tick;
            memcpy(trace->ticks[slot][lane], mine->ticks, sizeof(mine->ticks));
        }
        if (lane == 0 && (!round || at <= BUSTER_FILES_TRACE_ROUNDS))
        {
            trace->waves[slot] = run->waves;
            trace->probes[slot] = run->probes_made;
        }
        if (lane == 0 && round)
        {
            const uint32_t r = slot - 1;
            const uint32_t working = files_working_lanes(run);
            trace->rounds = slot;
            trace->walked = at;
            trace->files[r] += run->record_count;
            trace->bytes[r] += run->record_count != 0 ? run->costs[run->record_count - 1] : 0;
            trace->working[r] = working > trace->working[r] ? working : trace->working[r];
        }
    }
}

// Runs ahead on every lane of the group, which all call it: the roots are looked up, then prepared in round 0, and while
// the run follows includes, each round's new files' literal includes are searched for and the files found prepared in
// the next round, until a round finds none. Nothing it does reports a diagnostic. run->failed says if memory ran out, in
// which case the files it didn't prepare are left for the units to prepare.
static void files_run_ahead(FilesRun* run)
{
    const uint32_t lane = lane_index();
    FilesLane* mine = &run->lanes[lane];
    mine->tick = __rdtsc();
    files_trace(run, mine, lane, 0, false);
    files_fail(run, !files_lane_begin(run, mine));
    files_sync(mine, FILES_PHASE_ALONE);
    // Lane 0's word, the same on every lane: a lane that read run->failed itself could read lane 0's failure in
    // files_search_requests below, skip files_search and leave the others waiting at its syncs.
    const bool ready = files_broadcast(mine, FILES_PHASE_ALONE, lane == 0 && !__atomic_load_n(&run->failed, __ATOMIC_RELAXED)) != 0;
    if (ready && lane == 0)
    {
        files_root_requests(run);
        files_search_requests(run);
        files_tick(mine, FILES_PHASE_ALONE);
    }
    files_sync(mine, FILES_PHASE_SEARCH);
    if (ready)
    {
        files_search(run);
    }
    if (ready && lane == 0)
    {
        files_next_round(run);
        files_tick(mine, FILES_PHASE_ALONE);
    }
    bool more = files_broadcast(mine, FILES_PHASE_SEARCH, lane == 0 && ready && !run->failed && run->item_count > run->round_items) != 0;
    uint32_t round = 0;
    bool directories_listed = false;
    while (more)
    {
        round += 1;
        files_trace(run, mine, lane, round, true);
        if (run->record_count != 0)
        {
            files_prepare(run);
        }
        if (lane == 0 && run->roots_prepared == 0)
        {
            run->roots_prepared = __rdtsc();
        }
        arena_pop_to(&mine->names, 0);
        files_read_requests(run, mine);
        files_sync(mine, FILES_PHASE_REQUESTS);
        if (lane == 0)
        {
            files_search_requests(run);
            // The probes only read the snapshots. Build them on one lane before the first wave of include probes,
            // inside the measured walk; flat walks and files with no literal includes need no listing.
            if (!directories_listed && run->request_count != 0)
            {
                for (uint32_t d = 0; d < run->search->directory_count; d += 1)
                {
                    file_cache_list_directory(run->cache, run->search->directories[d], run->search->directory_lengths[d]);
                }
                directories_listed = true;
            }
            files_tick(mine, FILES_PHASE_ALONE);
        }
        files_sync(mine, FILES_PHASE_SEARCH);
        files_search(run);
        if (lane == 0)
        {
            files_next_round(run);
            files_tick(mine, FILES_PHASE_ALONE);
        }
        more = files_broadcast(mine, FILES_PHASE_SEARCH, lane == 0 && !run->failed && run->item_count > run->round_items) != 0;
    }
    files_trace(run, mine, lane, (round < BUSTER_FILES_TRACE_ROUNDS ? round : BUSTER_FILES_TRACE_ROUNDS) + 1, false);
}

// The entry point lane_run runs on each lane.
static __attribute__((unused)) void files_run_lane(void* argument)
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
        .shared_bytes = BUSTER_FILES_SHARED_BYTES,
        .lane_bytes = BUSTER_FILES_LANE_BYTES,
        .split_bytes = BUSTER_FILES_SPLIT_BYTES,
        .bytes = BUSTER_FILES_BYTES,
        .read_limit = BUSTER_FILES_READ_LIMIT,
        .lane_count = lane_count,
        .partition_count = BUSTER_FILES_PARTITIONS_PER_LANE * lane_count,
    };
    run->arena = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->files = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->round = arena_reserve(BUSTER_FILES_ARENA_BYTES);
    run->lanes = arena_push_array(&run->arena, lane_count, sizeof(FilesLane), 64);
    run->counts = arena_push_array(&run->arena, (uint64_t)lane_count * run->partition_count, sizeof(uint32_t), 64);
    run->partition_starts = arena_push_array(&run->arena, (uint64_t)run->partition_count + 1, sizeof(uint64_t), 64);
    run->item_arena = arena_reserve((uint64_t)BUSTER_FILES_MAX_ITEMS * sizeof(FilesItem) + 64);
    run->item_slot_arena = arena_reserve(2ull * BUSTER_FILES_MAX_ITEMS * sizeof(uint32_t) + 64);
    run->record_item_arena = arena_reserve((uint64_t)BUSTER_PP_MAX_FILES * sizeof(uint32_t) + 64);
    run->queued_arena = arena_reserve(BUSTER_PP_MAX_FILES + 64);
    run->items = arena_push_array(&run->item_arena, BUSTER_FILES_MAX_ITEMS, sizeof(FilesItem), 64);
    run->item_slots = arena_push_array(&run->item_slot_arena, 2ull * BUSTER_FILES_MAX_ITEMS, sizeof(uint32_t), 64);
    run->record_items = arena_push_array(&run->record_item_arena, BUSTER_PP_MAX_FILES, sizeof(uint32_t), 64);
    run->queued = arena_push(&run->queued_arena, BUSTER_PP_MAX_FILES, 64);
    run->item_slot_mask = BUSTER_FILE_CACHE_INITIAL_SLOTS - 1;
    // In a group of more than BUSTER_FILES_HUGE_LANES lanes, the token columns of the lanes past lane 0 start with base
    // pages, in the arenas the cache reserves the first time a lane asks for them.
    cache->lane_small_bytes = lane_count > BUSTER_FILES_HUGE_LANES ? BUSTER_FILES_COLUMN_SMALL_BYTES : 0;
    for (uint32_t lane = 0; lane < lane_count && lane < BUSTER_LANE_MAX_COUNT; lane += 1)
    {
        file_cache_arenas(cache, lane);
    }
    bool result = lane_count >= 1 && lane_count <= BUSTER_LANE_MAX_COUNT && run->lanes != 0 && run->counts != 0 && run->partition_starts != 0 && run->items != 0 &&
                  run->item_slots != 0 && run->record_items != 0 && run->queued != 0 && run->files.base != 0 && run->round.base != 0;
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
        arena_release(&lane->shared_arena);
        arena_release(&lane->lists);
        arena_release(&lane->names);
        arena_release(&lane->request_list);
        arena_release(&lane->path_list);
    }
    arena_release(&run->arena);
    arena_release(&run->item_arena);
    arena_release(&run->item_slot_arena);
    arena_release(&run->record_item_arena);
    arena_release(&run->queued_arena);
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
