// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #113's deferred bodies. check_input_deferred checks a unit as check_input does, with the same CheckInput and TypeTable,
// but reads every function body apart from the top level, out of order and on any number of lanes:
//   - The top level is read first: check_input_run with every function body deferred, on a table of its own that records
//     its changes (TypeChanges). Where the walk skips a body, check_deferred_cut records the reader's counts there: its
//     types and tags, its binding events and active declarations, and each table the walk appends to.
//   - After the walk, check_deferred_bodies reads the bodies on lanes: one, and one more per
//     BUSTER_CHECK_DEFERRED_LANE_TOKENS (and one per BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS of the top level's tokens) of
//     the bodies' tokens beyond the larger of the largest body's and twice the second largest's, no more than bodies.
//     It cuts the bodies into parts before the lanes start (parts_by_cost), promotes the part containing the largest
//     body for the first lane, and every lane takes the others from one counter, costliest first (check_deferred_lane):
//     the others are woken for the bodies and read them more slowly. A lane copies the top
//     level's reader when it takes its first part, so that one that takes none copies nothing
//     (check_deferred_lane_prepare), and sets it back for each body to what source order's reader is at the body's
//     opening (check_deferred_body): the file scope's bindings from their history (#319's environment, c_environment.c)
//     for the names the body and its parameters spell, its table a view of the top level's that hides the types the top
//     level made later and withdraws the definitions it made later (type_copy, TypeTable.hidden_begin,
//     type_tag_withdraw), and its tables the top level's, which the body appends to. A body's walk writes its own
//     tokens' columns, which no other walk writes, and the lane's copies, from which its result is taken: its binding
//     events, type changes, appended entries and the tags it completes; then the lane takes its copies back.
//   - The merge, in source order (check_deferred_merge): the type changes replayed into the caller's table
//     (type_replay), each body's events and entries put at its cut, where source order makes them, and every TypeId and
//     every id into those tables that the walk wrote mapped to its place. check_input_run's later phases then run on the
//     merged reader, as they do after a walk in source order.
// A body that does what the merge doesn't take yet sends the unit to source order: a declaration with linkage that
// changes what later ones see (its entity's first, one that changes its type, one unlike the one it replaces), a
// union of the top level's made transparent, a report, or a table that runs out of room. check_input_deferred then
// checks the unit with check_input, from nothing, and says so (CheckDeferred.unsupported). So does a unit with
// overloadable functions, whose sets later declarations join.
//
// The memory is the state's (CheckDeferredMemory), and outlives its readings: the top level's table and the lanes' are
// the deferred reader's own. The first reading that needs an arena reserves it, a lane's on the calling thread before
// the lanes start (check_deferred_lane_arenas), and every reading takes the arenas it uses back to empty, so that a
// state's readings after its first reserve nothing and touch pages already in place. The lanes' records that the input
// points to (attributes, asm texts, string bytes, typeof jobs) stay in their arenas until the state's next reading or
// check_deferred_release, which unmaps the memory.
//
// Include after c_check_input.c, which declares check_deferred_cut and check_deferred_bodies, and after lane.c and
// c_environment.c.

#include <stddef.h>
#include <time.h>

#define BUSTER_CHECK_DEFERRED_MAX_LANES 64
// The bodies' tokens that pay for a lane past the first, beyond their critical path (check_deferred_bodies): this many,
// and one more per BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS of the top level's tokens, since a lane's copy of the reader
// grows with the top level (docs/c-deferred-bodies.md, "How many lanes" and "The rare columns' pages by the unit's
// size").
#define BUSTER_CHECK_DEFERRED_LANE_TOKENS 1024
#define BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS 4
// The unit's tokens from which check_input_run's arena of rare columns is base pages where more than one lane reads the
// bodies (check_deferred_shared, #764, #847). In a smaller unit the top level's walk faults in most of the arena's huge
// pages, the bodies' lanes write into them at no cost, and the phases after the merge read the huge zero page where
// nothing wrote; a lane claims each of the others before its first write into it (check_input_rare_claim), so that no
// two lanes clear one at once. In a larger one, the top level's walk would clear a huge page for each 2 MB its few
// entries fall in.
#define BUSTER_CHECK_DEFERRED_BASE_PAGE_TOKENS 48640
// A lane's arenas (CheckDeferredArenas), in this order in one mapping: its reader's records, its scratch and its
// bodies' results of 64 GB each, its copy of the top level's table of 16 GB, the table's index of 4 GB and its per-name
// columns of 4 GB.
#define BUSTER_CHECK_DEFERRED_LANE_ARENAS 6
#define BUSTER_CHECK_DEFERRED_LANE_BYTES ((UINT64_C(3) << 36) + (UINT64_C(1) << 34) + (UINT64_C(2) << 32))
// Linux's MADV_POPULATE_WRITE (5.14), which the C library's headers may not name: a lane's per-name columns committed
// before its bodies (check_deferred_lane_commit).
#define BUSTER_CHECK_DEFERRED_MADV_POPULATE_WRITE 23

// The top level's counts where its walk skipped a body.
typedef struct CheckDeferredCut CheckDeferredCut;
struct CheckDeferredCut
{
    uint32_t opening;
    uint32_t closing;
    uint32_t types;
    uint32_t tags;
    uint32_t events;
    uint32_t actives;
    uint32_t strings;
    uint32_t regions;
    uint32_t vlas;
    uint32_t parameter_vla_begin;
    uint32_t asms;
    uint32_t varargs;
    uint32_t constants;
    uint32_t intrinsic_declarations;
    uint32_t intrinsic_calls;
};

// One of the top level's updates of a name's linked declaration (an id + 1) and type, at `position`
// (check_deferred_linked).
typedef struct CheckDeferredLinked CheckDeferredLinked;
struct CheckDeferredLinked
{
    uint32_t name;
    uint32_t position;
    uint32_t declaration;
    TypeId type;
};

// What a body's walk made, in its lane's ids: its type changes, its events' columns, its appended entries, the
// completion positions and transparency of the tags it made, the intrinsic declarations it made and its calls of
// intrinsics, the top level's VLAs it changed (its parameters') and the predefined types it made.
typedef struct CheckDeferredResult CheckDeferredResult;
struct CheckDeferredResult
{
    CheckWarningEvents warning_events;
    CheckConstValues const_values;
    TypeChanges changes;
    uint32_t types;
    uint32_t tags;
    uint32_t* tag_completions;
    uint32_t* transparent;
    // The top level's tags the body completed, its definition's parameter-list tags (#436), with their positions.
    uint32_t* completed_tags;
    uint32_t* completed_positions;
    uint32_t completed_count;
    CheckInputIntrinsicDeclaration* intrinsic_declarations;
    uint32_t intrinsic_declaration_count;
    CheckInputIntrinsicCall* intrinsic_calls;
    uint32_t intrinsic_call_count;
    uint32_t event_count;
    uint32_t* key;
    uint32_t* declaration;
    uint32_t* point;
    uint32_t* end;
    uint32_t* previous;
    uint32_t* depth;
    uint8_t* operation;
    uint8_t* kind;
    CheckInitString* strings;
    uint32_t string_count;
    CheckInputRegion* regions;
    uint32_t region_count;
    CheckInputVla* vlas;
    uint32_t vla_count;
    uint32_t* changed_vla_indices;
    CheckInputVla* changed_vlas;
    uint32_t changed_vla_count;
    CheckInputAsm* asms;
    uint32_t asm_count;
    CheckInputVarargs* varargs;
    uint32_t vararg_count;
    CheckValue* constants;
    uint32_t constant_count;
    TypeId predefined[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
    // Why the body is for source order, when it is.
    const char* reason;
    // The walk's state the body leaves behind: its __func__ strings' cache, and how far its initializers inferred.
    uint32_t function_name_body;
    uint32_t function_name_ids[3];
    uint32_t inference_end;
    // The lane that read the body, for the tests and the benchmark.
    uint32_t lane;
    bool unsupported;
};

// A lane's arenas, in one mapping that the state keeps across its readings (check_deferred_lane_arenas): its reader's
// records and scratch, its bodies' results, its copy of the top level's table and the table's index, and its per-name
// columns, at places they keep from one reading to the next (check_deferred_lane_names): the names they have room for,
// their bytes, the names whose entries are committed (the columns' and the intrinsics' cache's), the last body's stamp,
// and whether the columns that start as none may hold anything else (the tests' poison).
typedef struct CheckDeferredArenas CheckDeferredArenas;
struct CheckDeferredArenas
{
    uint8_t* mapping;
    uint64_t mapping_bytes;
    Arena output;
    Arena scratch;
    Arena results;
    Arena types;
    Arena type_index;
    Arena names;
    uint32_t name_room;
    uint32_t stamp;
    uint64_t name_bytes;
    uint32_t names_committed;
    uint32_t intrinsics_committed;
    bool names_dirty;
};

// The state's memory, which its readings share: the reader's own arena, the top level's table and its record of
// changes, the table's index, and each lane's arenas.
typedef struct CheckDeferredMemory CheckDeferredMemory;
struct CheckDeferredMemory
{
    Arena arena;
    Arena top;
    Arena top_index;
    CheckDeferredArenas lanes[BUSTER_CHECK_DEFERRED_MAX_LANES];
};

// A lane's copy of the top level's reader, made once and set back for each body.
typedef struct CheckDeferredLane CheckDeferredLane;
struct CheckDeferredLane
{
    CheckInputReader reader;
    CheckInput input;
    TypeTable types;
    // The lane's arenas (CheckDeferredMemory's), which its copy, its bodies' walks and their results take.
    CheckDeferredArenas* arenas;
    // The lane took a part and copied the reader for it (`ready`: the copy fits).
    bool copied;
    bool ready;
    // The top level's withdrawals (CheckDeferred.withdrawals) from which on the lane's table has them withdrawn: its
    // bodies come in runs in source order, so each moves the cursor rather than withdrawing everything again.
    uint32_t withdrawn_from;
    // The position checks' columns (check_deferred_heads): each name's last body (its index + 1) and, for the body
    // being read, its names' ordinary and tag keys, the opening as each key's point, and the history's handles.
    uint32_t* stamps;
    uint32_t* keys;
    uint32_t* points;
    uint32_t* handles;
    uint32_t key_count;
    // The top level's event that each of the body's proxies copies (check_deferred_heads), for its result's ids.
    uint32_t* proxies;
    // The record of a body's type changes, made once with room for the largest body and taken back for each: a body
    // keeps its changes packed in `results`, so that its walk touches the same pages as the last body's.
    TypeChanges changes;
    // What the lane did (CheckDeferred's counts), summed after the lanes.
    uint64_t bodies;
    uint64_t body_tokens;
    uint64_t history_reads;
    uint64_t withdrawn;
    uint64_t position_ns;
    uint64_t walk_ns;
    uint64_t copy_ns;
    // With `timing`, when the lane arrived (check_deferred_lane): CLOCK_MONOTONIC's nanoseconds, for CheckDeferred's
    // dispatch_ns and wake_ns.
    uint64_t arrived_ns;
};

struct CheckDeferred
{
    uint32_t lanes;
    // The caller's table, which the merge fills, and the top level's own, which records its changes.
    TypeTable* table;
    TypeTable top;
    TypeChanges top_changes;
    // The top level's completions and transparency, by position: the changes a body before them withdraws. A tag has
    // one completion at most and one transparency at most, after it; `transparencies` has each tag's place in the list,
    // BUSTER_CHECK_NONE for none.
    uint32_t* withdrawals;
    uint32_t withdrawal_count;
    uint32_t* transparencies;
    // Every body's cut, in source order, and its result.
    CheckDeferredCut* cuts;
    CheckDeferredResult* results;
    uint32_t body_count;
    uint32_t body_capacity;
    // The top level's updates of names' linked declarations and types (check_deferred_linked), in the walk's order and
    // room for two a token; then by name (check_deferred_bodies), each name's from linked_offsets[name] to
    // linked_offsets[name + 1] in position order, from which a lane takes the state at its body's opening
    // (check_deferred_name_state).
    CheckDeferredLinked* linked;
    uint32_t linked_count;
    uint32_t linked_capacity;
    CheckDeferredLinked* linked_names;
    uint32_t* linked_offsets;
    // The largest body's tokens (the first of them), the largest of the others' and the lane that read the largest; the
    // top level's tokens (the unit's outside its bodies), and the bodies' tokens beyond their critical path that paid for
    // a lane (check_deferred_bodies).
    uint32_t largest_body;
    uint32_t second_body;
    uint32_t largest_index;
    uint32_t largest_lane;
    uint32_t largest_parameters;
    uint32_t top_tokens;
    uint32_t lane_tokens;
    const CheckInputReader* top_reader;
    CEnvironment environment;
    CheckDeferredLane lane_states[BUSTER_CHECK_DEFERRED_MAX_LANES];
    // The bodies by cost, and the lanes' parts, cut before the lanes start (parts_by_cost): the parts' boundaries, the
    // order to take them in, the costliest first, and how many.
    uint64_t* costs;
    uint64_t* boundaries;
    uint32_t* order;
    uint64_t parts;
    // The state's memory, which outlives its readings: the one field check_input_deferred keeps of a reading before.
    CheckDeferredMemory memory;
    // The unit is for source order, and why.
    bool unsupported;
    const char* reason;
    // What the reader did: bodies read apart, their tokens, the heads read from the file scope's history, and the lanes
    // that copied the top level's reader (those that took a part).
    uint64_t bodies;
    uint64_t body_tokens;
    uint64_t history_reads;
    uint64_t withdrawn;
    uint64_t copies;
    // With `timing` (benchmark-checker-deferred), nanoseconds: the top level's walk with the setup before it, the file
    // scope's history (the environment's index), the lanes, the merge; and summed over the lanes, the bodies' walks and
    // their position checks: setting each body's heads from the history and its table's view, and taking them back. And
    // what a lane costs: its copy of the reader, summed over the lanes that copied it; lane_run's dispatch, from its call
    // to the first lane's arrival (the first lane's pinning, and the others' wake calls); their wake-up, from the first
    // lane's arrival to the last one's, none on one lane, which no lane waits for: the parts are cut before the lanes
    // start; and before the lanes, their arenas reserved or taken back.
    bool timing;
    // The lanes' CPUs, which a caller whose lane team is pinned (lane_team_start) sets for lane_run to name; 0 for none.
    const uint32_t* cpus;
    // Every lane the bodies can take, however few tokens they have, as the tests and test-checker-corpus read them.
    bool every_lane;
    // For the tests (test-checker-deferred): the merge fails right after its replay into the caller's table, as one
    // that runs out of room there does, and the unit goes to source order; with `hold_lanes`, every lane but the first
    // waits to take a part until the first has taken them all, so that the others must copy nothing; and with
    // `late_first` (not with hold_lanes), the first lane starts its part only once every other lane has taken from the
    // counter, as when the others arrive first, so that it must still read the costliest part.
    bool fail_after_replay;
    bool hold_lanes;
    bool late_first;
    // For the tests: the merge finds the token columns' entries by its scalar twin (check_deferred_entries).
    bool scalar_merge;
    // The first lane's word that it has taken every part, which hold_lanes' other lanes wait for; and the other lanes
    // that have taken from the counter, which late_first's first lane waits for.
    uint32_t held;
    uint32_t others_took;
    // The lanes the bodies ran on, as the first records it: `lanes`, or fewer when the bodies' tokens pay for fewer or the
    // bodies are fewer.
    uint32_t active;
    // The lanes the rule gives the bodies, worked out from the parser's tree before the top level's walk
    // (check_input_deferred), which `active` must be: check_input_run reserves its arena of rare columns by whether they
    // are more than one in a large unit (check_deferred_shared).
    uint32_t planned;
    // The unit has BUSTER_CHECK_DEFERRED_BASE_PAGE_TOKENS tokens or more (or `base_page_tokens`, for the tests and
    // benchmark-checker-deferred, where it isn't 0): where more than one lane reads, the rare columns in base pages.
    bool large;
    uint32_t base_page_tokens;
    // Whether the lanes claimed the rare columns' 2 MB regions before their first writes into them, as in a unit that
    // isn't large read on more than one lane (check_input_rare_claim), and how many regions they claimed.
    bool claiming;
    uint32_t claimed;
    uint64_t top_ns;
    uint64_t environment_ns;
    uint64_t lanes_ns;
    uint64_t merge_ns;
    // With `timing`, the merge's parts (check_deferred_merge): the replay; the appended tables, their layouts, copies
    // and intrinsics; the events' stream and the indices into it (the heads and the active declarations); the tags'
    // completions; the pass over the events and every token's columns, on the lanes; and the rest, the names' linked
    // types, the predefined types and the reader's fields.
    uint64_t merge_replay_ns;
    uint64_t merge_tables_ns;
    uint64_t merge_events_ns;
    uint64_t merge_tags_ns;
    uint64_t merge_pass_ns;
    uint64_t merge_rest_ns;
    uint64_t walk_ns;
    uint64_t position_ns;
    uint64_t copy_ns;
    uint64_t dispatch_ns;
    uint64_t wake_ns;
    uint64_t arenas_ns;
};

// CLOCK_MONOTONIC's nanoseconds, for `timing`'s spans.
static uint64_t check_deferred_now(void)
{
    struct timespec time = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static void check_deferred_cut(CheckInputReader* reader, uint32_t opening)
{
    CheckDeferred* deferred = reader->deferred;
    const bool room = deferred->body_count < deferred->body_capacity;
    if (room)
    {
        deferred->cuts[deferred->body_count] = (CheckDeferredCut){
            .opening = opening,
            .closing = reader->parser->tree.parents[opening],
            .types = reader->types->types.count,
            .tags = reader->types->tags.count,
            .events = reader->bindings.count,
            .actives = reader->bindings.active_count,
            .strings = reader->output->string_count,
            .regions = reader->output->region_count,
            .vlas = reader->output->vla_count,
            .parameter_vla_begin = reader->parameter_vla_begin,
            .asms = reader->output->asm_count,
            .varargs = reader->vararg_count,
            .constants = reader->cached_constant_count,
            .intrinsic_declarations = reader->output->intrinsic_declaration_count,
            .intrinsic_calls = reader->output->intrinsic_call_count,
        };
        deferred->body_count += 1;
    }
    deferred->unsupported |= !room;
    deferred->reason = !room ? "more bodies than the mask has" : deferred->reason;
}

static void check_deferred_linked(CheckInputReader* reader, uint32_t name, uint32_t position)
{
    CheckDeferred* deferred = reader->deferred;
    const bool room = deferred->linked_count < deferred->linked_capacity;
    if (room)
    {
        deferred->linked[deferred->linked_count] = (CheckDeferredLinked){
            .name = name,
            .position = position,
            .declaration = reader->linked_declarations[name],
            .type = reader->linked_types[name],
        };
        deferred->linked_count += 1;
    }
    deferred->unsupported |= !room;
    deferred->reason = !room ? "more linked declarations than the log has" : deferred->reason;
}

// Pushes `count` records of `size` bytes, copies `source`'s first `used` into them, and returns them (0 without room).
static void* check_deferred_copy(Arena* arena, const void* source, uint64_t used, uint64_t count, uint64_t size)
{
    void* result = count != 0 ? arena_push(arena, count * size, 64) : 0;
    if (result != 0 && used != 0)
    {
        memcpy(result, source, used * size);
    }
    return result;
}

// `arena` taken back to empty, reserved with `capacity` first if it has no memory: the state's own arena and the top
// level's table's, which its first reading reserves on the calling thread. In base pages throughout, as the lanes' are
// (check_deferred_lane_arenas): the top level's table has the caller's capacities, each column room for every type the
// unit could make, far apart, and the state's arena a log of the top level's linked updates with room for two a token,
// of which a reading touches a little at each one's start. In huge pages each start's first touch cleared a 2 MB page,
// as the driver's table's columns did (#528): on the 9700X, main's reader read sixteen bodies of 993 tokens after three
// headers 27% faster on one lane in base pages, and 43% on eight (#697).
static void check_deferred_arena(Arena* arena, uint64_t capacity)
{
    if (arena->base == 0)
    {
        *arena = arena_reserve_small(capacity, capacity);
    }
    arena_pop_to(arena, 0);
}

// A lane's arenas taken back to empty, reserved first if the state hasn't run the lane yet: whether it has them. They
// are reserved on the calling thread before the lanes start, since reserving memory takes the process's address-space
// lock for writing, on which lanes that reserve at once wait for each other: on the 9700X a lane's copy of the reader
// took 101 µs on two lanes and 264 µs on eight (the medians of 42 units) when each lane reserved its five arenas
// itself, an mmap and two madvise calls each, and 69 and 102 µs reserved here, which takes 2 to 20 µs
// (docs/c-deferred-bodies.md, "How many lanes"). So they are one mapping, in base pages throughout: a lane touches its
// copies of the top level's columns, with room for the largest body that it mostly leaves untouched, and its bodies'
// results, a few megabytes on sqlite3.c, where a huge page would be cleared whole the first time any of it is touched.
static bool check_deferred_lane_arenas(CheckDeferredArenas* arenas)
{
    Arena* list[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { &arenas->output, &arenas->scratch, &arenas->results, &arenas->types, &arenas->type_index, &arenas->names };
    const uint64_t capacities[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 34, UINT64_C(1) << 32, UINT64_C(1) << 32 };
    if (arenas->mapping == 0)
    {
        uint64_t limit = arena_reservation_limit();
        uint64_t total = 0;
        for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
        {
            total += capacities[i] < limit ? capacities[i] : limit;
        }
        // A single inaccessible reservation retains the lane's stable contiguous layout, without charging its
        // unused hundreds of GiB against CommitLimit. Each slice commits only its own pushes. Release the original
        // mapping directly: the parent descriptor does not track the children's writable prefixes.
        Arena storage = arena_reserve_exact(total, total);
#if !BUSTER_SANITIZER_RESOURCES
        if (storage.base == 0 && (errno == ENOMEM || errno == EINVAL) && limit > BUSTER_ARENA_COMPACT_BYTES)
        {
            arena_compact = true;
            limit = arena_reservation_limit();
            total = 0;
            for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
            {
                total += capacities[i] < limit ? capacities[i] : limit;
            }
            storage = arena_reserve_exact(total, total);
        }
#endif
        if (storage.base != 0)
        {
            arenas->mapping = storage.base;
            arenas->mapping_bytes = storage.mapping_bytes;
            uint64_t offset = 0;
            for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
            {
                const uint64_t capacity = capacities[i] < limit ? capacities[i] : limit;
                *list[i] = (Arena){
                    .base = storage.base + offset,
                    .capacity = capacity,
                    .small = capacity,
                    .mapping_bytes = capacity,
                    .requested_capacity = capacities[i],
                };
                offset += capacity;
            }
        }
    }
    for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
    {
        arena_pop_to(list[i], 0);
    }
    return arenas->mapping != 0;
}

// The pages of `column`'s entries from `from` to `to`, each of `bytes`, committed (MADV_POPULATE_WRITE).
static void check_deferred_commit(void* column, uint32_t from, uint32_t to, uint64_t bytes)
{
    const uintptr_t begin = ((uintptr_t)column + from * bytes) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    const uintptr_t end = ((uintptr_t)column + to * bytes + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    if (to > from)
    {
        // The advice only commits the pages, zeroed as a fault would: the columns work whether or not the kernel takes
        // it.
        madvise((void*)begin, end - begin, BUSTER_CHECK_DEFERRED_MADV_POPULATE_WRITE);
    }
}

// The pages of the entries a reading of `names` names takes in the lane's per-name columns, committed past those an
// earlier reading took, each column's in one call (MADV_POPULATE_WRITE; the intrinsics' cache only where the reader has
// one): the heads' first three times `names` and the others' first `names`. A body's position checks set a few entries
// of every column for the names it spells, far apart, and their faults, a few at each body, took the position checks on
// #697's units to up to 7 times main's on one lane and 14 times on eight, whose copies of the top level's columns
// committed theirs together.
static void check_deferred_lane_commit(CheckDeferredLane* lane, uint32_t names)
{
    CheckDeferredArenas* arenas = lane->arenas;
    CheckInputReader* reader = &lane->reader;
    const uint32_t from = arenas->names_committed;
    if (names > from)
    {
        check_deferred_commit(reader->bindings.heads, from * C_BINDING_NAMESPACES, names * C_BINDING_NAMESPACES, sizeof(uint32_t));
        check_deferred_commit(reader->parameter_seen, from, names, sizeof(uint32_t));
        check_deferred_commit(reader->member_seen, from, names, sizeof(uint32_t));
        check_deferred_commit(lane->stamps, from, names, sizeof(uint32_t));
        check_deferred_commit(reader->linked_declarations, from, names, sizeof(uint32_t));
        check_deferred_commit(reader->linked_types, from, names, sizeof(TypeId));
        check_deferred_commit(reader->builtin_names, from, names, sizeof(uint16_t));
        arenas->names_committed = names;
    }
    if (reader->intrinsic_names != 0 && names > arenas->intrinsics_committed)
    {
        check_deferred_commit(reader->intrinsic_names, arenas->intrinsics_committed, names, sizeof(uint32_t));
        arenas->intrinsics_committed = names;
    }
}

// The lane's per-name columns, in its names arena at places they keep from one reading to the next, so that a lane
// copies none of the top level's:
//   - The columns every body leaves as none come first: the binding heads and the parameters' and members' marks, which
//     a walk takes back as its scopes close and check_deferred_heads_clear after it, for the names the body spells.
//   - Then the stamps, which only grow from body to body and reading to reading (check_deferred_heads).
//   - Then the linked declarations and types and the intrinsics' and builtins' caches, which each body sets for the names
//     it spells before its walk reads them (check_deferred_heads, check_deferred_name_state).
// A reading with more names than the places have room for lays them out again, on zero pages, or on the old ones cleared
// where the kernel doesn't take them back. The entries of a reading's names are committed before its bodies
// (check_deferred_lane_commit). The tests' poison (check_deferred_poison) leaves the columns that start as none dirty,
// and the next reading clears them. Whether the columns fit.
static bool check_deferred_lane_names(const CheckDeferred* deferred, CheckDeferredLane* lane, uint32_t names)
{
    CheckDeferredArenas* arenas = lane->arenas;
    CheckInputReader* reader = &lane->reader;
    Arena* arena = &arenas->names;
    uint64_t room = arenas->name_room;
    if (names > room)
    {
        room = room != 0 ? room : 4096;
        while (room < names)
        {
            room *= 2;
        }
        if (arenas->name_bytes != 0 && madvise(arena->base, arenas->name_bytes, MADV_DONTNEED) != 0)
        {
            // The kernel kept the pages (it refuses the advice for a locked range): the new layout reads its columns
            // that start as none as zero, so the old one is cleared.
            memset(arena->base, 0, arenas->name_bytes);
        }
        arenas->name_bytes = 0;
        arenas->names_committed = 0;
        arenas->intrinsics_committed = 0;
        arenas->stamp = 0;
        arenas->names_dirty = false;
    }
    // A name's three heads, its six columns of four bytes and the builtins' two, with each column's alignment.
    bool valid = room <= UINT32_MAX && room * (C_BINDING_NAMESPACES * 4 + 6 * 4 + 2) + 10 * 64 <= arena->capacity;
    arena_pop_to(arena, 0);
    uint32_t* heads = valid ? arena_push_array(arena, room * C_BINDING_NAMESPACES, sizeof(uint32_t), 64) : 0;
    uint32_t* parameter_seen = heads != 0 ? arena_push_array(arena, room, sizeof(uint32_t), 64) : 0;
    uint32_t* member_seen = parameter_seen != 0 ? arena_push_array(arena, room, sizeof(uint32_t), 64) : 0;
    uint32_t* stamps = member_seen != 0 ? arena_push_array(arena, room, sizeof(uint32_t), 64) : 0;
    const uint64_t none_bytes = arena->length;
    uint32_t* linked_declarations = stamps != 0 ? arena_push_array(arena, room, sizeof(uint32_t), 64) : 0;
    TypeId* linked_types = linked_declarations != 0 ? arena_push_array(arena, room, sizeof(TypeId), 64) : 0;
    uint32_t* intrinsic_names = linked_types != 0 ? arena_push_array(arena, room, sizeof(uint32_t), 64) : 0;
    uint16_t* builtin_names = intrinsic_names != 0 ? arena_push_array(arena, room, sizeof(uint16_t), 64) : 0;
    valid = builtin_names != 0;
    if (valid)
    {
        if (arenas->names_dirty)
        {
            memset(arena->base, 0, none_bytes);
            arenas->stamp = 0;
            arenas->names_dirty = false;
        }
        arenas->name_room = (uint32_t)room;
        arenas->name_bytes = arena->length;
        reader->bindings.heads = heads;
        reader->parameter_seen = parameter_seen;
        reader->member_seen = member_seen;
        reader->linked_declarations = linked_declarations;
        reader->linked_types = linked_types;
        reader->intrinsic_names = deferred->top_reader->intrinsic_names != 0 ? intrinsic_names : 0;
        reader->builtin_names = builtin_names;
        lane->stamps = stamps;
        check_deferred_lane_commit(lane, names);
    }
    return valid;
}

// A lane's copy of the top level's reader, with room for the largest body, in its arenas: whether it fits.
static bool check_deferred_lane_prepare(CheckDeferred* deferred, CheckDeferredLane* lane)
{
    const CheckInputReader* top = deferred->top_reader;
    const CheckInput* top_input = top->output;
    const uint32_t names = top->bindings.name_capacity;
    const uint64_t room = (uint64_t)deferred->largest_body + deferred->largest_parameters + 64;
    CheckDeferredArenas* arenas = lane->arenas;
    const TypeCapacity extra = {
        .types = (uint32_t)(room * 8),
        .parameters = (uint32_t)(room * 8),
        .tags = (uint32_t)room,
        .members = (uint32_t)(room * 2),
    };
    bool valid = arenas->mapping != 0 && type_copy(&lane->types, &deferred->top, &arenas->types, &arenas->type_index, extra);
    lane->reader = *top;
    lane->input = *top_input;
    CheckInputReader* reader = &lane->reader;
    CheckInput* input = &lane->input;
    reader->output = input;
    reader->types = &lane->types;
    reader->arena = &arenas->output;
    reader->scratch = &arenas->scratch;
    reader->warning_query_arena = &arenas->results;
    reader->warning_events = (CheckWarningBuffer){ .arena = &arenas->output };
    reader->const_values = (CheckConstBuffer){ .arena = &arenas->output, .prefix = &top->output->const_values };
    input->const_values = check_const_view(&reader->const_values);
    reader->literal_warnings = top->literal_warnings;
    reader->literal_warnings.arena = &arenas->output;
    reader->literal_warnings.borrowed = true;
    reader->source_warning_keys = top->source_warning_keys;
    reader->source_warning_keys.arena = &arenas->output;
    reader->source_warning_keys.borrowed = true;
    reader->variable_sized_records = top->variable_sized_records;
    reader->variable_sized_records.arena = &arenas->output;
    reader->variable_sized_records.borrowed = true;
    input->warning_events = (CheckWarningEvents){ 0 };
    reader->deferred = 0;
    reader->lane = true;
    // Token columns are shared over disjoint bodies; interval summaries have shared ancestors and must be private.
    reader->warning_index_ready = false;
    reader->warning_index = valid ? check_deferred_copy(&arenas->output, 0, 0, (uint64_t)reader->warning_index_base * 2, sizeof(uint16_t)) : 0;
    valid &= reader->warning_index != 0;
    input->deferred_tokens = 0;
    Arena* arena = &arenas->output;
    // The lane's events, none of them the top level's: a body's stream starts with the file scope's events its heads
    // reach (check_deferred_heads, a proxy for each, two names' keys a name at most) and the top level's last event,
    // then the body's own.
    const uint64_t proxies = room * 2 + 1;
    const uint64_t events = proxies + room * 4;
    CBindings* bindings = &reader->bindings;
    bindings->capacity = valid && events < UINT32_MAX ? (uint32_t)events : 0;
    // The heads, the per-name columns and the stamps, in the lane's places for them (check_deferred_lane_names).
    valid = valid && check_deferred_lane_names(deferred, lane, names);
    bindings->active = valid ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->key = bindings->active != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->declaration = bindings->key != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->point = bindings->declaration != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->end = bindings->point != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->previous = bindings->end != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->depth = bindings->previous != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->operation = bindings->depth != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint8_t)) : 0;
    bindings->kind = bindings->operation != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint8_t)) : 0;
    lane->proxies = bindings->kind != 0 ? check_deferred_copy(arena, 0, 0, proxies, sizeof(uint32_t)) : 0;
    valid = lane->proxies != 0;
    if (valid)
    {
        bindings->base = 0;
        bindings->count = 0;
        bindings->active_count = 0;
        bindings->valid = true;
    }
    // The per-tag arrays the walk writes, the scope stacks and the appended tables, the lane's own.
    const uint64_t tags = (uint64_t)lane->types.tags.capacity;
    reader->tag_completions = valid ? check_deferred_copy(arena, top->tag_completions, top->types->tags.count, tags, sizeof(uint32_t)) : 0;
    // Whether each enum has a fixed base: the top level's for its tags, and the lane's own for those its bodies make,
    // which take the indices after the top level's and are set where each is made (check_input_tag_identity).
    reader->enum_fixed = reader->tag_completions != 0 ? check_deferred_copy(arena, top->enum_fixed, top->types->tags.count, tags, sizeof(uint8_t)) : 0;
    valid = reader->enum_fixed != 0;
    if (valid && top->intrinsic_names != 0)
    {
        // The intrinsics' tables, after the top level's of which each body appends its own declarations and calls; their
        // cache, which each body sets for the names it spells as source order's is at its opening
        // (check_deferred_name_state) and fills for those it decodes, is among the lane's per-name columns.
        // A body decodes each name once at most, a name the top level decoded after its opening among them: room for the
        // top level's declarations and as many again as the top level has room for.
        const uint64_t declarations = (uint64_t)top_input->intrinsic_declaration_count + top_input->intrinsic_declaration_capacity;
        input->intrinsic_declaration_capacity = declarations < UINT32_MAX ? (uint32_t)declarations : UINT32_MAX;
        input->intrinsic_declarations = check_deferred_copy(arena, top_input->intrinsic_declarations, top_input->intrinsic_declaration_count,
                                                            (uint64_t)input->intrinsic_declaration_capacity + 1, sizeof(CheckInputIntrinsicDeclaration));
        input->intrinsic_calls = check_deferred_copy(arena, top_input->intrinsic_calls, top_input->intrinsic_call_count,
                                                     (uint64_t)top_input->intrinsic_call_capacity + 1, sizeof(CheckInputIntrinsicCall));
        valid = input->intrinsic_declarations != 0 && input->intrinsic_calls != 0;
    }
    if (valid && top->transparent_unions != 0)
    {
        reader->transparent_unions = check_deferred_copy(arena, top->transparent_unions, top->types->tags.count, tags, sizeof(uint32_t));
        valid = reader->transparent_unions != 0;
    }
    lane->withdrawn_from = deferred->withdrawal_count;
    // Two keys for each name the body and its parameters spell, at most.
    const uint64_t keys = room * 2;
    valid = valid && type_changes_initialize(&lane->changes, &arenas->results, (uint32_t)(room * 8 + 64), (uint32_t)(room * 8 + 64), (uint32_t)(room * 2 + 64));
    lane->keys = valid ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    lane->points = lane->keys != 0 ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    lane->handles = lane->points != 0 ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    valid = lane->handles != 0;
    reader->scope_marks = valid ? check_deferred_copy(arena, 0, 0, room + 2, sizeof(uint32_t)) : 0;
    reader->scope_ends = reader->scope_marks != 0 ? check_deferred_copy(arena, 0, 0, room + 2, sizeof(uint32_t)) : 0;
    reader->scope_openings = reader->scope_ends != 0 ? check_deferred_copy(arena, 0, 0, room + 2, sizeof(uint32_t)) : 0;
    input->report_capacity = (uint32_t)room;
    input->reports = reader->scope_openings != 0 ? check_deferred_copy(arena, 0, 0, room, sizeof(CheckInputReport)) : 0;
    // Strings have no capacity of their own: a literal makes at most one, as the unit's count bounds them.
    input->strings = input->reports != 0 ? check_deferred_copy(arena, top_input->strings, top_input->string_count, top_input->string_count + room, sizeof(CheckInitString)) : 0;
    // Statement expressions' regions have none either.
    input->regions = input->strings != 0 ? check_deferred_copy(arena, top_input->regions, top_input->region_count, top_input->region_count + room, sizeof(CheckInputRegion)) : 0;
    input->vla_capacity = (uint32_t)(top_input->vla_count + room);
    input->vlas = input->regions != 0 ? check_deferred_copy(arena, top_input->vlas, top_input->vla_count, input->vla_capacity, sizeof(CheckInputVla)) : 0;
    input->asm_capacity = (uint32_t)(top_input->asm_count + room);
    input->asm_statements = input->vlas != 0 ? check_deferred_copy(arena, top_input->asm_statements, top_input->asm_count, input->asm_capacity, sizeof(CheckInputAsm)) : 0;
    reader->vararg_capacity = (uint32_t)(top->vararg_count + room);
    reader->varargs = input->asm_statements != 0 ? check_deferred_copy(arena, top->varargs, top->vararg_count, reader->vararg_capacity, sizeof(CheckInputVarargs)) : 0;
    const uint64_t constants = (uint64_t)top->cached_constant_count + room;
    reader->cached_constants = reader->varargs != 0 ? check_deferred_copy(arena, top->cached_constants, top->cached_constant_count, constants, sizeof(CheckValue)) : 0;
    valid = reader->cached_constants != 0;
    return valid;
}

// The file scope's declaration of `key` that source order's reader sees at `point`, given the history's last one
// activated at or before it (`handle`, an event + 1, 0 for none): that one, unless a restore ended it at or before the
// point, then the latest of the name's earlier ones that is still active, or none. Only a declarator that fails ends
// one, whose unit has a report and is source order's: a tag or an enumeration constant first declared in a file-scope
// prototype (`void g(struct S *p);`) is of the prototype's scope, a level deeper than the file scope the history holds
// (#438).
static uint32_t check_deferred_visible(const CEnvironment* environment, uint32_t key, uint32_t point, uint32_t handle)
{
    const CBindings* bindings = environment->bindings;
    const uint32_t begin = environment->offsets[key];
    uint32_t slot = environment->offsets[key + 1];
    while (slot > begin && environment->handles[slot - 1] != handle)
    {
        slot -= 1;
    }
    // The handle's place in the name's history (slot - 1); one the history doesn't hold is none.
    uint32_t result = slot > begin ? handle : 0;
    while (result != 0 && bindings->end[result - 1] != BUSTER_C_BINDING_NONE && bindings->point[bindings->end[result - 1]] <= point)
    {
        slot -= 1;
        result = slot > begin ? environment->handles[slot - 1] : 0;
    }
    return result;
}

// The end of the updates at or before `point` within one name's position-ordered history. Equal positions keep the
// last logged update (a declarator followed by its initializer). A body may spell a name with many later declarations;
// binary search bounds each lookup by the history's logarithm instead of scanning its later updates again per body.
static uint32_t check_deferred_linked_end(const CheckDeferredLinked* linked, uint32_t begin, uint32_t end, uint32_t point)
{
    while (begin < end)
    {
        const uint32_t middle = begin + (end - begin) / 2;
        if (linked[middle].position <= point)
        {
            begin = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return begin;
}

// Sets the lane's state of `name` that the top level's walk changes as it goes to source order's at the body's opening
// (`cut`): its linked declaration and type, the last of the top level's updates before the opening
// (check_deferred_linked), and its intrinsic's cache entry, which a declaration the top level made after the cut
// leaves undecoded, as source order's is there. The walk reads them for the names it spells alone.
static void check_deferred_name_state(const CheckDeferred* deferred, CheckDeferredLane* lane, uint32_t name, const CheckDeferredCut* cut)
{
    CheckInputReader* reader = &lane->reader;
    uint32_t declaration = 0;
    TypeId type = 0;
    if (deferred->linked_offsets != 0)
    {
        const uint32_t begin = deferred->linked_offsets[name];
        const uint32_t slot = check_deferred_linked_end(deferred->linked_names, begin, deferred->linked_offsets[name + 1], cut->opening);
        declaration = slot > begin ? deferred->linked_names[slot - 1].declaration : 0;
        type = slot > begin ? deferred->linked_names[slot - 1].type : 0;
    }
    reader->linked_declarations[name] = declaration;
    reader->linked_types[name] = type;
    if (reader->intrinsic_names != 0)
    {
        const CheckInputReader* top = deferred->top_reader;
        const uint32_t cached = top->intrinsic_names[name];
        const bool later = cached != 0 && (cached & UINT32_C(0x80000000)) == 0 && cached - 1 >= cut->intrinsic_declarations;
        const BuiltinIntrinsicInfo* info = later ? top->output->intrinsic_declarations[cached - 1].info : 0;
        reader->intrinsic_names[name] = later ? UINT32_C(0x80000000) | (uint32_t)(info - builtin_intrinsic_infos) : cached;
    }
}

// The lane's event `slot` a copy of the file scope's `event`: a proxy of it in the body's stream (check_deferred_heads).
static void check_deferred_proxy(CBindings* bindings, uint32_t slot, const CBindings* file, uint32_t event)
{
    bindings->key[slot] = file->key[event];
    bindings->declaration[slot] = file->declaration[event];
    bindings->point[slot] = file->point[event];
    bindings->end[slot] = file->end[event];
    bindings->previous[slot] = file->previous[event];
    bindings->depth[slot] = file->depth[event];
    bindings->operation[slot] = file->operation[event];
    bindings->kind[slot] = file->kind[event];
}

// Sets the lane's heads of every name the body (`cut`'s) and its parameters spell to the file scope's at the body's
// opening: the binding state source order's reader has there, for the names the body can look up. Each name once (by
// the lane's stamps: the body's is one past the last body's, on the lane, in this reading or an earlier one), its label
// head none and its ordinary and tag heads from the history, 16 keys at a time (c_environment_lookup_batch), each checked
// against the restores (check_deferred_visible, which the rare declaration that ended takes); and its other per-name
// columns that the walk reads, from the top level's (check_deferred_name_state, and the builtins' cache). The keys
// stay for check_deferred_heads_clear. Whether the history took every key.
static bool check_deferred_heads(CheckDeferred* deferred, CheckDeferredLane* lane, const CheckDeferredCut* cut)
{
    CheckInputReader* reader = &lane->reader;
    const CheckInputReader* top = deferred->top_reader;
    const Parser* parser = reader->parser;
    CBindings* bindings = &reader->bindings;
    const uint32_t names = bindings->name_capacity;
    CheckDeferredArenas* arenas = lane->arenas;
    arenas->stamp += 1;
    if (arenas->stamp == 0)
    {
        // The stamps wrapped: none of them is a body's again.
        memset(lane->stamps, 0, (uint64_t)arenas->name_room * sizeof(uint32_t));
        arenas->stamp = 1;
    }
    const uint32_t stamp = arenas->stamp;
    uint32_t count = 0;
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        uint32_t token = pass == 0 ? cut->opening : reader->parameters[cut->opening];
        while (token != BUSTER_CHECK_NONE && (pass == 1 || token <= cut->closing))
        {
            const uint32_t name = parser->tokens.names[token];
            if (name != 0 && name < names && lane->stamps[name] != stamp)
            {
                lane->stamps[name] = stamp;
                lane->keys[count] = C_BINDING_ORDINARY * names + name;
                lane->keys[count + 1] = C_BINDING_TAG * names + name;
                lane->points[count] = cut->opening;
                lane->points[count + 1] = cut->opening;
                bindings->heads[C_BINDING_LABEL * names + name] = 0;
                reader->builtin_names[name] = top->builtin_names[name];
                check_deferred_name_state(deferred, lane, name, cut);
                count += 2;
            }
            token = pass == 0 ? token + 1 : reader->parameter_next[token];
        }
    }
    const CEnvironment* environment = &deferred->environment;
    const CBindings* file = environment->bindings;
    const bool valid = c_environment_lookup_batch(environment, lane->keys, lane->points, count, lane->handles);
    // Each head the history gives is the top level's event, which the body's stream gets a proxy of, in its order: the
    // walk reads the file scope's events at its heads alone. Then the top level's last event before the body, whose
    // point the walk reads as the stream's last before its first own event (check_input_vla_bound's `cached`), as in
    // source order's stream there. A copy of the top level's whole stream gave the top level's last event, after the
    // body where the top level goes on, so that a bound's names read before the body's first event seemed read
    // already and the body reported an error source order doesn't: such a unit went to source order.
    uint32_t proxy = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t handle = valid ? lane->handles[i] : 0;
        const bool ended = handle != 0 && file->end[handle - 1] != BUSTER_C_BINDING_NONE;
        handle = ended ? check_deferred_visible(environment, lane->keys[i], cut->opening, handle) : handle;
        if (handle != 0)
        {
            check_deferred_proxy(bindings, proxy, file, handle - 1);
            lane->proxies[proxy] = handle - 1;
            proxy += 1;
            handle = proxy;
        }
        bindings->heads[lane->keys[i]] = handle;
    }
    if (cut->events != 0)
    {
        check_deferred_proxy(bindings, proxy, file, cut->events - 1);
        lane->proxies[proxy] = cut->events - 1;
        proxy += 1;
    }
    bindings->base = proxy;
    bindings->count = proxy;
    lane->key_count = count;
    lane->history_reads += count / 2;
    return valid;
}

// Takes the heads check_deferred_heads set, and what the walk left in them and in the parameters' and members' marks,
// back to none: the walk writes them for the names the body spells alone, and the next body, in this reading or a later
// one, takes them as none (check_deferred_lane_names).
static void check_deferred_heads_clear(CheckDeferredLane* lane)
{
    CheckInputReader* reader = &lane->reader;
    CBindings* bindings = &reader->bindings;
    const uint32_t names = bindings->name_capacity;
    for (uint32_t i = 0; i < lane->key_count; i += 2)
    {
        const uint32_t name = lane->keys[i] - C_BINDING_ORDINARY * names;
        bindings->heads[C_BINDING_ORDINARY * names + name] = 0;
        bindings->heads[C_BINDING_TAG * names + name] = 0;
        bindings->heads[C_BINDING_LABEL * names + name] = 0;
        reader->parameter_seen[name] = 0;
        reader->member_seen[name] = 0;
    }
    lane->key_count = 0;
}

// Sets the lane's table to have the top level's withdrawals from `first` on withdrawn, and those before given back: the
// completions and transparency the top level made after a body's opening, which source order's table hasn't there. A
// tag's two withdrawals commute (a completion's clears its transparency too), so the lane moves from the withdrawals it
// has to these by the difference alone. Giving back a completion gives back the tag's final columns, its transparency
// among them, which comes out again where the top level made it after the opening.
static void check_deferred_withdraw_to(CheckDeferred* deferred, CheckDeferredLane* lane, uint32_t first)
{
    const TypeChanges* changes = &deferred->top_changes;
    TypeTable* types = &lane->types;
    for (uint32_t i = first; i < lane->withdrawn_from; i += 1)
    {
        const TypeChange* change = changes->items + deferred->withdrawals[i];
        type_tag_withdraw(types, change->tag, change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT);
    }
    for (uint32_t i = lane->withdrawn_from; i < first; i += 1)
    {
        const uint32_t tag = changes->items[deferred->withdrawals[i]].tag;
        const uint32_t transparency = deferred->transparencies[tag];
        type_tag_restore(types, &deferred->top, tag);
        if (transparency != BUSTER_CHECK_NONE && transparency >= first)
        {
            type_tag_withdraw(types, tag, true);
        }
    }
    lane->withdrawn += first > lane->withdrawn_from ? first - lane->withdrawn_from : lane->withdrawn_from - first;
    lane->withdrawn_from = first;
}

// Copies `count` records of `size` bytes from `source` into the arena: 0 when there are none or no room (which `valid`
// tells apart).
static void* check_deferred_keep(Arena* arena, const void* source, uint64_t count, uint64_t size, bool* valid)
{
    void* result = *valid && count != 0 ? arena_push(arena, count * size, 64) : 0;
    *valid = *valid && (count == 0 || result != 0);
    if (result != 0)
    {
        memcpy(result, source, count * size);
    }
    return result;
}

// Reads one body on its lane, from the top level's state at its opening, and keeps its result.
static void check_deferred_body(CheckDeferred* deferred, CheckDeferredLane* lane, uint32_t body)
{
    const CheckDeferredCut* cut = deferred->cuts + body;
    CheckDeferredResult* result = deferred->results + body;
    const CheckInputReader* top = deferred->top_reader;
    const CheckInput* top_input = top->output;
    CheckInputReader* reader = &lane->reader;
    CheckInput* input = &lane->input;
    TypeTable* types = &lane->types;
    const uint64_t setup_begin = deferred->timing ? check_deferred_now() : 0;
    // The walk's state at a file-scope function's opening brace, before its scope.
    reader->scope_count = 0;
    reader->prototype_depth = 0;
    reader->function_result = 0;
    reader->function_end = 0;
    reader->function_body = BUSTER_CHECK_NONE;
    reader->parameter_vla_begin = cut->parameter_vla_begin;
    reader->parameter_vla_end = cut->vlas;
    reader->specifier_owner = BUSTER_CHECK_NONE;
    reader->specifier_query_depth = 0;
    reader->specifier_pending = false;
    reader->function_name_body = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        reader->function_name_ids[i] = BUSTER_CHECK_NONE;
    }
    reader->inference_end = 0;
    reader->specifier_attributes = 0;
    reader->declarator_attributes = 0;
    reader->specifier_abi_origin = 0;
    reader->declarator_abi_origin = 0;
    reader->constant_abi_origin = 0;
    reader->specifier_thread_local = false;
    reader->declarator_overloadable = false;
    reader->lane_unsupported = false;
    reader->lane_reason = 0;
    reader->warning_events = (CheckWarningBuffer){ .arena = reader->arena };
    reader->const_values = (CheckConstBuffer){ .arena = reader->arena, .prefix = &top_input->const_values };
    input->const_values = check_const_view(&reader->const_values);
    reader->literal_warnings = top->literal_warnings;
    reader->literal_warnings.arena = reader->arena;
    reader->literal_warnings.borrowed = true;
    reader->warning_bound_open = BUSTER_CHECK_NONE;
    reader->source_warning_keys = top->source_warning_keys;
    reader->source_warning_keys.arena = reader->arena;
    reader->source_warning_keys.borrowed = true;
    reader->variable_sized_records = top->variable_sized_records;
    reader->variable_sized_records.arena = reader->arena;
    reader->variable_sized_records.borrowed = true;
    input->warning_events = (CheckWarningEvents){ 0 };
    reader->cached_constant_count = top->cached_constant_count;
    reader->vararg_count = top->vararg_count;
    input->report_count = 0;
    input->resource = false;
    input->string_count = top_input->string_count;
    input->region_count = top_input->region_count;
    input->vla_count = top_input->vla_count;
    input->asm_count = top_input->asm_count;
    input->intrinsic_declaration_count = top_input->intrinsic_declaration_count;
    input->intrinsic_call_count = top_input->intrinsic_call_count;
    for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        const bool made = top->predefined_positions[i] <= cut->opening;
        reader->predefined_types[i] = made ? top->predefined_types[i] : 0;
        reader->predefined_positions[i] = made ? top->predefined_positions[i] : BUSTER_CHECK_NONE;
    }
    // The body's stream starts with its proxies (check_deferred_heads), none active.
    reader->bindings.active_count = 0;
    reader->bindings.valid = true;
    const bool history = check_deferred_heads(deferred, lane, cut);
    // The table as the top level's was at the opening: its later types hidden, its later definitions withdrawn.
    const TypeViewMark view = type_view_mark(types);
    types->hidden_begin = cut->types;
    types->hidden_end = deferred->top.types.count;
    reader->lane_tags = view.tags;
    // The first withdrawal after the opening, by binary search: the list is in position order.
    uint32_t low = 0;
    uint32_t high = deferred->withdrawal_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool after = deferred->top_changes.items[deferred->withdrawals[middle]].position > cut->opening;
        high = after ? middle : high;
        low = after ? low : middle + 1;
    }
    check_deferred_withdraw_to(deferred, lane, low);
    const uint64_t tokens = (uint64_t)cut->closing - cut->opening + 1;
    // The lane's record, empty, with the room it was made with.
    TypeChanges* changes = &lane->changes;
    *changes = (TypeChanges){
        .items = changes->items,
        .capacity = changes->capacity,
        .parameters = changes->parameters,
        .parameter_capacity = changes->parameter_capacity,
        .members = changes->members,
        .member_capacity = changes->member_capacity,
    };
    bool valid = history && tokens * 8 + 64 <= changes->capacity;
    types->changes = valid ? changes : 0;
    const uint64_t walk_begin = deferred->timing ? check_deferred_now() : 0;
    if (valid)
    {
        check_input_resolve_range(reader, cut->opening, cut->closing + 1);
    }
    const uint64_t walk_end = deferred->timing ? check_deferred_now() : 0;
    types->changes = 0;
    // The completions of tags the top level made: a definition's parameter-list tags, which its body declares again
    // (#436) and nothing after the body can name, so that only the merged table and the tags' positions show them. A
    // tag the top level completes itself, which a lane withdraws, isn't one.
    uint32_t completed = 0;
    bool completes = false;
    for (uint32_t i = 0; i < changes->count; i += 1)
    {
        const TypeChange* change = changes->items + i;
        const bool top_tag = (change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM) && change->tag < view.tags;
        completed += top_tag;
        completes |= top_tag && (deferred->top.tags.completion[change->tag] != 0 || deferred->transparencies[change->tag] != BUSTER_CHECK_NONE);
    }
    result->unsupported = !valid || reader->lane_unsupported || input->report_count != 0 || input->resource || !reader->bindings.valid || changes->full ||
                          completes;
    result->reason = !valid || input->resource || changes->full ? "a body ran out of room" : reader->lane_unsupported ? reader->lane_reason :
                     input->report_count != 0 ? "a body reports an error" :
                     !reader->bindings.valid ? "a body's bindings ran out of room" : completes ? "a body completes a tag the top level completes" : 0;
    if (!result->unsupported)
    {
        Arena* arena = &lane->arenas->results;
        const uint32_t base = reader->bindings.base;
        const uint32_t events = reader->bindings.count - base;
        bool kept = true;
        const CheckWarningEvents warnings = check_warning_view(&reader->warning_events);
        result->warning_events = (CheckWarningEvents){
            .events = check_deferred_keep(arena, warnings.events, warnings.count, sizeof(CheckWarningEvent), &kept),
            .count = warnings.count,
            .notes = check_deferred_keep(arena, warnings.notes, warnings.note_count, sizeof(CheckWarningNote), &kept),
            .note_count = warnings.note_count,
        };
        const CheckConstValues values = check_const_view(&reader->const_values);
        CheckConstPart* const_parts = check_deferred_keep(arena, values.parts, values.part_count, sizeof(CheckConstPart), &kept);
        for (uint32_t p = 0; kept && p < values.part_count; p += 1)
        {
            if (const_parts[p].kind == CHECK_CONST_BYTES)
            {
                const_parts[p].bytes = check_deferred_keep(arena, const_parts[p].bytes, const_parts[p].byte_count, 1, &kept);
            }
        }
        result->const_values = (CheckConstValues){
            .objects = check_deferred_keep(arena, values.objects, values.count, sizeof(CheckConstObject), &kept),
            .count = values.count,
            .parts = const_parts,
            .part_count = values.part_count,
            .unions = check_deferred_keep(arena, values.unions, values.union_count, sizeof(CheckConstUnion), &kept),
            .union_count = values.union_count,
            .hash = check_deferred_keep(arena, values.hash, values.hash_count, sizeof(CheckConstHash), &kept),
            .hash_count = values.hash_count,
            .hash_root = values.hash_root,
        };
        // The body's changes, packed: the record's arrays are the lane's, which its next body takes back.
        result->changes = *changes;
        result->changes.items = check_deferred_keep(arena, changes->items, changes->count, sizeof(TypeChange), &kept);
        result->changes.capacity = changes->count;
        result->changes.parameters = check_deferred_keep(arena, changes->parameters, changes->parameter_count, sizeof(TypeId), &kept);
        result->changes.parameter_capacity = changes->parameter_count;
        result->changes.members = check_deferred_keep(arena, changes->members, changes->member_count, sizeof(TypeMember), &kept);
        result->changes.member_capacity = changes->member_count;
        result->types = types->types.count - view.types;
        result->tags = types->tags.count - view.tags;
        result->tag_completions = check_deferred_keep(arena, reader->tag_completions + view.tags, result->tags, sizeof(uint32_t), &kept);
        result->transparent = reader->transparent_unions != 0 ? check_deferred_keep(arena, reader->transparent_unions + view.tags, result->tags, sizeof(uint32_t), &kept) : 0;
        result->completed_count = completed;
        result->completed_tags = kept && completed != 0 ? arena_push_array(arena, completed, sizeof(uint32_t), 4) : 0;
        result->completed_positions = result->completed_tags != 0 ? arena_push_array(arena, completed, sizeof(uint32_t), 4) : 0;
        kept = kept && (completed == 0 || result->completed_positions != 0);
        completed = 0;
        for (uint32_t i = 0; kept && i < changes->count; i += 1)
        {
            const TypeChange* change = changes->items + i;
            if ((change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM) && change->tag < view.tags)
            {
                result->completed_tags[completed] = change->tag;
                result->completed_positions[completed] = reader->tag_completions[change->tag];
                completed += 1;
            }
        }
        // A unit with intrinsics has the lane's tables (check_deferred_lane_prepare); one without has none.
        const bool intrinsics = reader->intrinsic_names != 0;
        result->intrinsic_declaration_count = input->intrinsic_declaration_count - top_input->intrinsic_declaration_count;
        result->intrinsic_declarations = intrinsics ? check_deferred_keep(arena, input->intrinsic_declarations + top_input->intrinsic_declaration_count,
                                                                          result->intrinsic_declaration_count, sizeof(CheckInputIntrinsicDeclaration), &kept) : 0;
        result->intrinsic_call_count = input->intrinsic_call_count - top_input->intrinsic_call_count;
        result->intrinsic_calls = intrinsics ? check_deferred_keep(arena, input->intrinsic_calls + top_input->intrinsic_call_count, result->intrinsic_call_count,
                                                                   sizeof(CheckInputIntrinsicCall), &kept) : 0;
        result->event_count = events;
        result->key = check_deferred_keep(arena, reader->bindings.key + base, events, sizeof(uint32_t), &kept);
        result->declaration = check_deferred_keep(arena, reader->bindings.declaration + base, events, sizeof(uint32_t), &kept);
        result->point = check_deferred_keep(arena, reader->bindings.point + base, events, sizeof(uint32_t), &kept);
        result->end = check_deferred_keep(arena, reader->bindings.end + base, events, sizeof(uint32_t), &kept);
        result->previous = check_deferred_keep(arena, reader->bindings.previous + base, events, sizeof(uint32_t), &kept);
        result->depth = check_deferred_keep(arena, reader->bindings.depth + base, events, sizeof(uint32_t), &kept);
        result->operation = check_deferred_keep(arena, reader->bindings.operation + base, events, sizeof(uint8_t), &kept);
        result->kind = check_deferred_keep(arena, reader->bindings.kind + base, events, sizeof(uint8_t), &kept);
        // The ids in the body's events, as the merge reads them: an own event's the top level's count past its place
        // after the proxies, and a proxy's (a declaration a body's one shadows) the top level's event it copies.
        const uint32_t top_events = top->bindings.count;
        for (uint32_t e = 0; kept && e < events; e += 1)
        {
            const uint32_t previous = result->previous[e];
            result->previous[e] = previous == 0 ? 0 : previous <= base ? lane->proxies[previous - 1] + 1 : previous - base + top_events;
            const uint32_t end = result->end[e];
            result->end[e] = end == BUSTER_C_BINDING_NONE ? end : end - base + top_events;
        }
        result->string_count = input->string_count - top_input->string_count;
        result->strings = check_deferred_keep(arena, input->strings + top_input->string_count, result->string_count, sizeof(CheckInitString), &kept);
        result->region_count = input->region_count - top_input->region_count;
        result->regions = check_deferred_keep(arena, input->regions + top_input->region_count, result->region_count, sizeof(CheckInputRegion), &kept);
        result->vla_count = input->vla_count - top_input->vla_count;
        result->vlas = check_deferred_keep(arena, input->vlas + top_input->vla_count, result->vla_count, sizeof(CheckInputVla), &kept);
        result->asm_count = input->asm_count - top_input->asm_count;
        result->asms = check_deferred_keep(arena, input->asm_statements + top_input->asm_count, result->asm_count, sizeof(CheckInputAsm), &kept);
        result->vararg_count = reader->vararg_count - top->vararg_count;
        result->varargs = check_deferred_keep(arena, reader->varargs + top->vararg_count, result->vararg_count, sizeof(CheckInputVarargs), &kept);
        result->constant_count = reader->cached_constant_count - top->cached_constant_count;
        result->constants = check_deferred_keep(arena, reader->cached_constants + top->cached_constant_count, result->constant_count, sizeof(CheckValue), &kept);
        // The top level's VLAs the body's start changed: its parameters'.
        uint32_t changed = 0;
        for (uint32_t i = 0; i < top_input->vla_count; i += 1)
        {
            changed += memcmp(input->vlas + i, top_input->vlas + i, sizeof(CheckInputVla)) != 0;
        }
        result->changed_vla_count = changed;
        result->changed_vla_indices = kept && changed != 0 ? arena_push_array(arena, changed, sizeof(uint32_t), 4) : 0;
        result->changed_vlas = kept && changed != 0 ? arena_push_array(arena, changed, sizeof(CheckInputVla), 8) : 0;
        kept = kept && (changed == 0 || (result->changed_vla_indices != 0 && result->changed_vlas != 0));
        changed = 0;
        for (uint32_t i = 0; kept && i < top_input->vla_count; i += 1)
        {
            if (memcmp(input->vlas + i, top_input->vlas + i, sizeof(CheckInputVla)) != 0)
            {
                result->changed_vla_indices[changed] = i;
                result->changed_vlas[changed] = input->vlas[i];
                input->vlas[i] = top_input->vlas[i];
                changed += 1;
            }
        }
        for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            const bool made = reader->predefined_types[i] != 0 && top->predefined_positions[i] > cut->opening;
            result->predefined[i] = made ? reader->predefined_types[i] : 0;
        }
        result->function_name_body = reader->function_name_body;
        for (uint32_t i = 0; i < 3; i += 1)
        {
            result->function_name_ids[i] = reader->function_name_ids[i];
        }
        result->inference_end = reader->inference_end;
        result->unsupported = !kept;
        result->reason = !kept ? "a body's result ran out of room" : result->reason;
    }
    // The lane back to the top level's state.
    const uint64_t undo_begin = deferred->timing ? check_deferred_now() : 0;
    check_deferred_heads_clear(lane);
    const bool reset = type_view_reset(types, view);
    // The top level's tags the body changed, back to the top level's state: the view takes back the members a
    // completion appended, which the tag would still name, though nothing after the body can reach a parameter-list
    // tag (none of them is one the top level withdraws: a body doesn't change one where it is merged).
    for (uint32_t i = 0; i < changes->count; i += 1)
    {
        const TypeChange* change = changes->items + i;
        const bool completion = change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM ||
                                change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT;
        if (completion && change->tag < view.tags)
        {
            type_tag_restore(types, &deferred->top, change->tag);
            reader->tag_completions[change->tag] = top->tag_completions[change->tag];
            reader->enum_fixed[change->tag] = top->enum_fixed[change->tag];
            if (reader->transparent_unions != 0)
            {
                reader->transparent_unions[change->tag] = top->transparent_unions[change->tag];
            }
        }
    }
    for (uint32_t i = 0; i < top_input->vla_count; i += 1)
    {
        input->vlas[i] = top_input->vlas[i];
    }
    result->reason = !reset && !result->unsupported ? "a lane's table couldn't be taken back" : result->reason;
    result->unsupported |= !reset;
    lane->ready = reset;
    lane->bodies += 1;
    lane->body_tokens += tokens;
    const uint64_t undo_end = deferred->timing ? check_deferred_now() : 0;
    // The position checks: the setup before the walk (with its record's pushes) and the undo after it.
    lane->position_ns += (walk_begin - setup_begin) + (undo_end - undo_begin);
    lane->walk_ns += walk_end - walk_begin;
}

typedef struct CheckDeferredRun CheckDeferredRun;
struct CheckDeferredRun
{
    CheckDeferred* deferred;
};

// Reads a part's bodies on the lane. The lane copies the reader for its first part: a lane that takes none spends
// nothing on one.
static void check_deferred_part(CheckDeferred* deferred, CheckDeferredLane* lane, uint32_t index, uint32_t part)
{
    const uint64_t copy_begin = deferred->timing && !lane->copied ? check_deferred_now() : 0;
    lane->ready = lane->copied ? lane->ready : check_deferred_lane_prepare(deferred, lane);
    lane->copy_ns += copy_begin != 0 ? check_deferred_now() - copy_begin : 0;
    lane->copied = true;
    for (uint64_t body = deferred->boundaries[part]; body < deferred->boundaries[part + 1]; body += 1)
    {
        deferred->results[body].unsupported = true;
        deferred->results[body].reason = "a lane's copy of the reader ran out of room";
        deferred->results[body].lane = index;
        if (lane->ready)
        {
            check_deferred_body(deferred, lane, (uint32_t)body);
        }
    }
}

static void check_deferred_lane(void* argument)
{
    CheckDeferredRun* run = argument;
    CheckDeferred* deferred = run->deferred;
    const uint32_t index = lane_index();
    CheckDeferredLane* lane = deferred->lane_states + index;
    lane->arenas = deferred->memory.lanes + index;
    // No lane waits for another: the parts were cut before the lanes started.
    lane->arrived_ns = deferred->timing ? check_deferred_now() : 0;
    const uint64_t parts = deferred->parts;
    const uint32_t* order = deferred->order;
    while (deferred->hold_lanes && index != 0 && __atomic_load_n(&deferred->held, __ATOMIC_ACQUIRE) == 0)
    {
        // A test's wait, which yields: the tests may run more lanes than CPUs.
        sched_yield();
    }
    while (deferred->late_first && index == 0 && __atomic_load_n(&deferred->others_took, __ATOMIC_ACQUIRE) + 1 < lane_count())
    {
        // A test's wait for the other lanes' first takes, before the first lane's own.
        sched_yield();
    }
    // The part containing the largest body, order[0], is the first lane's own, which it starts at once. The others are
    // woken for the bodies, and a woken lane reads a body at about half the first lane's speed. The counter gives out the others,
    // costliest first, to every lane as it comes.
    uint64_t taken = index == 0 ? 0 : 1 + lane_take(parts - 1);
    if (deferred->late_first && index != 0)
    {
        __atomic_fetch_add(&deferred->others_took, 1, __ATOMIC_RELEASE);
    }
    for (; taken < parts; taken = 1 + lane_take(parts - 1))
    {
        check_deferred_part(deferred, lane, index, order[taken]);
    }
    if (index == 0)
    {
        deferred->active = lane_count();
        __atomic_store_n(&deferred->held, 1, __ATOMIC_RELEASE);
    }
}

// A part's map of ids into one of the walk's tables: the top level's ids below `top_count` go through `top`, a body's own
// (from top_count) to `base` and on.
static uint32_t check_deferred_id(const uint32_t* top, uint32_t top_count, uint32_t base, uint32_t id)
{
    return id < top_count ? top[id] : base + (id - top_count);
}

// The final place of every entry of an appended table: the top level's (top_map, count of them) after the bodies whose
// cuts come at or before them, and each body's (bases[b]) at its cut. `cut` is the field of CheckDeferredCut, by offset,
// and counts the bodies' entries. Returns the final count.
static uint32_t check_deferred_layout(const CheckDeferred* deferred, uint64_t cut_offset, const uint32_t* counts, uint32_t top_count, uint32_t* top_map,
                                      uint32_t* bases)
{
    uint32_t shift = 0;
    uint32_t body = 0;
    for (uint32_t id = 0; id <= top_count; id += 1)
    {
        while (body < deferred->body_count && *(const uint32_t*)((const uint8_t*)(deferred->cuts + body) + cut_offset) <= id)
        {
            bases[body] = id + shift;
            shift += counts[body];
            body += 1;
        }
        if (id < top_count)
        {
            top_map[id] = id + shift;
        }
    }
    return top_count + shift;
}

// Maps a TypeId through a part of the replay (0 stays 0).
static TypeId check_deferred_type(const TypeReplayPart* part, TypeId type)
{
    return type != 0 ? type_replay_type(part, type) : 0;
}

static bool check_deferred_const_values(CheckConstBuffer* output, const CheckConstValues* input, const TypeReplayPart* remap, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    bool valid = true;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        CheckConstObject object = input->objects[i];
        valid = object.part_first <= input->part_count && object.part_count <= input->part_count - object.part_first &&
                object.union_first <= input->union_count && object.union_count <= input->union_count - object.union_first;
        CheckConstPart* parts = valid && object.part_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckConstPart, object.part_count) : NULL;
        CheckConstUnion* unions = valid && object.union_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckConstUnion, object.union_count) : NULL;
        valid = valid && (object.part_count == 0 || parts != NULL) && (object.union_count == 0 || unions != NULL);
        if (valid)
        {
            object.type = check_deferred_type(remap, object.type);
            for (uint32_t p = 0; p < object.part_count; p += 1)
            {
                parts[p] = input->parts[object.part_first + p];
                parts[p].type = check_deferred_type(remap, parts[p].type);
                parts[p].value.type = check_deferred_type(remap, parts[p].value.type);
            }
            for (uint32_t u = 0; u < object.union_count; u += 1)
            {
                unions[u] = input->unions[object.union_first + u];
                unions[u].type = check_deferred_type(remap, unions[u].type);
            }
            valid = check_const_append(output, object, parts, object.part_count, unions, object.union_count, true);
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// The only TypeIds in sparse diagnostic payloads are explicit fields. Located tokens, completion boundaries and
// identifier IDs remain translation-unit identities. Attached notes are copied privately before remapping.
static bool check_deferred_warnings(CheckWarningBuffer* output, const CheckWarningEvents* input, const TypeReplayPart* part, Arena* scratch)
{
    bool valid = (input->count == 0 || input->events != NULL) && (input->note_count == 0 || input->notes != NULL);
    const uint64_t mark = scratch->length;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        CheckWarningEvent event = input->events[i];
        valid = event.note_first <= input->note_count && event.note_count <= input->note_count - event.note_first;
        CheckWarningNote* notes = valid && event.note_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningNote, event.note_count) : NULL;
        valid = valid && (event.note_count == 0 || notes != NULL);
        if (valid)
        {
            event.payload.first_type = check_deferred_type(part, event.payload.first_type);
            event.payload.second_type = check_deferred_type(part, event.payload.second_type);
            for (uint32_t n = 0; n < event.note_count; n += 1)
            {
                notes[n] = input->notes[event.note_first + n];
                notes[n].payload.first_type = check_deferred_type(part, notes[n].payload.first_type);
                notes[n].payload.second_type = check_deferred_type(part, notes[n].payload.second_type);
            }
            valid = check_warning_append(output, &event, notes, event.note_count);
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// Maps an id + 1 (0 for none) through check_deferred_id.
static uint32_t check_deferred_id1(const uint32_t* top, uint32_t top_count, uint32_t base, uint32_t id)
{
    return id != 0 ? check_deferred_id(top, top_count, base, id - 1) + 1 : 0;
}

// The tokens from `begin` to `end` of `column` that have an entry (a value other than `none`), in token order, into
// `positions`: how many. The walk writes each of the merge's columns at few tokens (on sqlite3.c, a declared type at
// 7% of them, a query type at 2% and the rest at fewer still), so the vector path tests 64 tokens at a time and takes
// those that have one lane by lane; the scalar path, its twin, tests each token. Every load is masked to the range.
// The merge writes a column back at its entries alone: the reader keeps these columns in pages of their own, which
// take memory only where the walk wrote them (#572), and writing their zeros back would make every page of zeros real.
static uint32_t check_deferred_entries(const uint32_t* column, uint32_t begin, uint32_t end, uint32_t none, bool vector, uint32_t* positions)
{
    uint32_t total = 0;
    if (vector)
    {
        const __m512i empty = _mm512_set1_epi32((int)none);
        for (uint32_t first = begin; first < end; first += 64)
        {
            uint64_t set = 0;
            for (uint32_t part = 0; part < 4; part += 1)
            {
                const uint32_t at = first + part * 16;
                const uint32_t length = at < end ? (end - at < 16 ? end - at : 16) : 0;
                const __mmask16 active = (__mmask16)(((uint32_t)1 << length) - 1);
                const __m512i values = length != 0 ? _mm512_maskz_loadu_epi32(active, column + at) : empty;
                set |= (uint64_t)_mm512_mask_cmpneq_epu32_mask(active, values, empty) << (part * 16);
            }
            while (set != 0)
            {
                positions[total] = first + (uint32_t)__builtin_ctzll(set);
                total += 1;
                set &= set - 1;
            }
        }
    }
    else
    {
        for (uint32_t token = begin; token < end; token += 1)
        {
            positions[total] = token;
            total += column[token] != none;
        }
    }
    return total;
}

// check_deferred_entries of a column of 64-bit values, whose entries are those other than 0.
static uint32_t check_deferred_entries64(const uint64_t* column, uint32_t begin, uint32_t end, bool vector, uint32_t* positions)
{
    uint32_t total = 0;
    if (vector)
    {
        for (uint32_t first = begin; first < end; first += 64)
        {
            uint64_t set = 0;
            for (uint32_t part = 0; part < 8; part += 1)
            {
                const uint32_t at = first + part * 8;
                const uint32_t length = at < end ? (end - at < 8 ? end - at : 8) : 0;
                const __mmask8 active = (__mmask8)(((uint32_t)1 << length) - 1);
                const __m512i values = length != 0 ? _mm512_maskz_loadu_epi64(active, column + at) : _mm512_setzero_si512();
                set |= (uint64_t)_mm512_mask_test_epi64_mask(active, values, values) << (part * 8);
            }
            while (set != 0)
            {
                positions[total] = first + (uint32_t)__builtin_ctzll(set);
                total += 1;
                set &= set - 1;
            }
        }
    }
    else
    {
        for (uint32_t token = begin; token < end; token += 1)
        {
            positions[total] = token;
            total += column[token] != 0;
        }
    }
    return total;
}

// How many of a byte column's tokens from `begin` to `end` are other than 0: 64 at a time, or with the scalar twin one
// by one.
static uint32_t check_deferred_count_bytes(const uint8_t* column, uint32_t begin, uint32_t end, bool vector)
{
    uint32_t total = 0;
    if (vector)
    {
        for (uint32_t first = begin; first < end; first += 64)
        {
            const uint32_t length = end - first < 64 ? end - first : 64;
            const __mmask64 active = length == 64 ? ~(__mmask64)0 : (((__mmask64)1 << length) - 1);
            const __m512i bytes = _mm512_maskz_loadu_epi8(active, column + first);
            total += (uint32_t)__builtin_popcountll(_mm512_mask_test_epi8_mask(active, bytes, bytes));
        }
    }
    else
    {
        for (uint32_t token = begin; token < end; token += 1)
        {
            total += column[token] != 0;
        }
    }
    return total;
}

// The owner of `token` in a pass over a column's entries in token order: b + 1 for body b's tokens and its parameters'
// (which its opening rebinds), 0 for the top level's. `body` is the pass's cursor, the first body that doesn't close
// before the token: a body's parameters come after the body before it closes and before it opens.
static uint32_t check_deferred_owner(const CheckDeferred* deferred, const CheckInputReader* reader, uint32_t token, uint32_t* body)
{
    uint32_t b = *body;
    while (b < deferred->body_count && deferred->cuts[b].closing < token)
    {
        b += 1;
    }
    *body = b;
    uint32_t owner = 0;
    if (b < deferred->body_count)
    {
        const CheckDeferredCut* cut = deferred->cuts + b;
        owner = cut->opening <= token ? b + 1 : 0;
        for (uint32_t p = reader->parameters[cut->opening]; owner == 0 && p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
        {
            owner = p == token ? b + 1 : 0;
        }
    }
    return owner;
}

// A column of ids into one of the walk's appended tables, which the pass maps at its entries through their owners'
// places in the table's layout (check_deferred_layout's `map` of the top level's `top` ids and the bodies' `bases`):
// ids, or with `one` 1 ids + 1, none being `none`.
typedef struct CheckDeferredIds CheckDeferredIds;
struct CheckDeferredIds
{
    uint32_t* column;
    uint32_t none;
    uint32_t one;
    const uint32_t* map;
    const uint32_t* bases;
    uint32_t top;
};

// The merge's pass over the events and the token columns, which the lanes that read the bodies share by whole items
// (check_deferred_pass_items): item b, for each body b, is the top level's tokens and events after body b - 1's and
// then body b's own, and item `bodies` the rest of the unit's. Every item writes places of its own: the merged stream's
// from the events' layout, and its tokens' entries in place.
typedef struct CheckDeferredPass CheckDeferredPass;
struct CheckDeferredPass
{
    const CheckDeferred* deferred;
    const CheckInputReader* reader;
    const TypeReplayPart* parts;
    // The items' tokens, as prefix sums: the lanes' shares by cost (lane_range_by_cost).
    const uint64_t* costs;
    // Room for every token's position, an item's tokens' at their own.
    uint32_t* positions;
    bool vector;
    // The top level's stream (the reader's), its layout, and the merged stream's columns.
    const uint32_t* event_map;
    const uint32_t* event_bases;
    uint32_t top_events;
    uint32_t* key;
    uint32_t* declaration;
    uint32_t* point;
    uint32_t* end;
    uint32_t* previous;
    uint32_t* depth;
    uint8_t* operation;
    uint8_t* kind;
    // The columns of TypeIds, and of ids into the tables some body appended to.
    TypeId* types[7];
    CheckDeferredIds ids[9];
    uint32_t id_count;
    // Each lane's tokens with a constant's warning.
    uint32_t warnings[BUSTER_CHECK_DEFERRED_MAX_LANES];
};

// Item `item`'s events: the top level's before its body's cut (the rest of them for the last item), each as many places
// on as the bodies before them have events, then the body's at its place; every index into the stream mapped, and a
// restore's mark past the top level's declarations active at the cut. The fields that hold no index are copied whole.
static void check_deferred_pass_events(const CheckDeferredPass* pass, uint32_t item)
{
    const CheckDeferred* deferred = pass->deferred;
    const CBindings* top = &pass->reader->bindings;
    const uint32_t* map = pass->event_map;
    const uint32_t bodies = deferred->body_count;
    const uint32_t from = item == 0 ? 0 : deferred->cuts[item - 1].events;
    const uint32_t to = item < bodies ? deferred->cuts[item].events : pass->top_events;
    if (from < to)
    {
        const uint32_t at = map[from];
        const uint64_t length = to - from;
        memcpy(pass->key + at, top->key + from, length * sizeof(uint32_t));
        memcpy(pass->declaration + at, top->declaration + from, length * sizeof(uint32_t));
        memcpy(pass->point + at, top->point + from, length * sizeof(uint32_t));
        memcpy(pass->depth + at, top->depth + from, length * sizeof(uint32_t));
        memcpy(pass->operation + at, top->operation + from, length);
        memcpy(pass->kind + at, top->kind + from, length);
        for (uint32_t e = from; e < to; e += 1)
        {
            const uint32_t last = top->end[e];
            const uint32_t before = top->previous[e];
            pass->end[at + (e - from)] = last != BUSTER_C_BINDING_NONE ? map[last] : BUSTER_C_BINDING_NONE;
            pass->previous[at + (e - from)] = before != 0 ? map[before - 1] + 1 : 0;
        }
    }
    const CheckDeferredResult* result = item < bodies ? deferred->results + item : 0;
    if (result != 0 && result->event_count != 0)
    {
        const uint32_t base = pass->event_bases[item];
        const uint32_t actives = deferred->cuts[item].actives;
        const uint64_t length = result->event_count;
        memcpy(pass->key + base, result->key, length * sizeof(uint32_t));
        memcpy(pass->point + base, result->point, length * sizeof(uint32_t));
        memcpy(pass->depth + base, result->depth, length * sizeof(uint32_t));
        memcpy(pass->operation + base, result->operation, length);
        memcpy(pass->kind + base, result->kind, length);
        for (uint32_t e = 0; e < result->event_count; e += 1)
        {
            const uint32_t last = result->end[e];
            pass->declaration[base + e] = result->operation[e] == C_BINDING_RESTORE ? result->declaration[e] + actives : result->declaration[e];
            pass->end[base + e] = last != BUSTER_C_BINDING_NONE ? check_deferred_id(map, pass->top_events, base, last) : BUSTER_C_BINDING_NONE;
            pass->previous[base + e] = check_deferred_id1(map, pass->top_events, base, result->previous[e]);
        }
    }
}

// Items `first` to `end` of the pass: their events, then each column's entries among their tokens, each through its
// owner's part (check_deferred_owner): a body's tokens and its parameters' through the body's, and the top level's
// through its own; a body's part maps the top level's ids as the top level's does. Returns the tokens among them with a
// constant's warning.
static uint32_t check_deferred_pass_items(const CheckDeferredPass* pass, uint32_t first, uint32_t end)
{
    const CheckDeferred* deferred = pass->deferred;
    const CheckInputReader* reader = pass->reader;
    const uint32_t bodies = deferred->body_count;
    const uint32_t count = reader->parser->tokens.count;
    for (uint32_t item = first; item < end; item += 1)
    {
        check_deferred_pass_events(pass, item);
    }
    // The items' tokens: from the end of body first - 1 to the end of body end - 1, or of the unit's last token.
    const uint32_t begin = first == end || first == 0 ? 0 : deferred->cuts[first - 1].closing + 1;
    const uint32_t finish = end == first ? begin : end <= bodies ? deferred->cuts[end - 1].closing + 1 : count + 1;
    uint32_t* positions = pass->positions + begin;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(pass->types); c += 1)
    {
        TypeId* column = pass->types[c];
        const uint32_t entries = check_deferred_entries(column, begin, finish, 0, pass->vector, positions);
        uint32_t body = first;
        for (uint32_t e = 0; e < entries; e += 1)
        {
            const uint32_t token = positions[e];
            column[token] = type_replay_type(&pass->parts[check_deferred_owner(deferred, reader, token, &body)], column[token]);
        }
    }
    for (uint32_t c = 0; c < pass->id_count; c += 1)
    {
        const CheckDeferredIds* ids = pass->ids + c;
        const uint32_t entries = check_deferred_entries(ids->column, begin, finish, ids->none, pass->vector, positions);
        uint32_t body = first;
        for (uint32_t e = 0; e < entries; e += 1)
        {
            const uint32_t token = positions[e];
            const uint32_t owner = check_deferred_owner(deferred, reader, token, &body);
            ids->column[token] = check_deferred_id(ids->map, ids->top, owner != 0 ? ids->bases[owner - 1] : 0, ids->column[token] - ids->one) + ids->one;
        }
    }
    const uint32_t jobs = check_deferred_entries64((const uint64_t*)reader->specifier_finishes, begin, finish, pass->vector, positions);
    uint32_t body = first;
    for (uint32_t e = 0; e < jobs; e += 1)
    {
        const uint32_t token = positions[e];
        const TypeReplayPart* part = &pass->parts[check_deferred_owner(deferred, reader, token, &body)];
        CheckInputSpecifierJob* job = reader->specifier_finishes[token];
        job->base = check_deferred_type(part, job->base);
        job->result = check_deferred_type(part, job->result);
    }
    // Lanes share disjoint token columns but own their reader counters: the count of the tokens with a constant's
    // warning, which later declaration-time warning mutations keep through the same helper.
    return check_deferred_count_bytes(reader->output->constant_warnings, begin, finish < count ? finish : count, pass->vector);
}

// A 2 MB region of the merged stream's columns, faulted in on the calling thread before the lanes write them: a
// store of 0 at the range's start and at every region's start it reaches, so that each region has its page before any
// lane writes it. The pass writes every event of the columns on the lanes that read the bodies, into fresh huge pages
// of the caller's scratch on a process's one reading, and lanes that write a fresh region at once each allocate and
// clear a huge page for it, of which the kernel maps one (#874, as #764's race in the walks). The stores land in pages
// already in place on a reading whose arenas are reused, and every byte they write the pass writes again.
static void check_deferred_fault_range(void* begin, uint64_t bytes)
{
    uint8_t* end = (uint8_t*)begin + bytes;
    uintptr_t region = (uintptr_t)begin & ~(BUSTER_ARENA_HUGE_PAGE_BYTES - 1);
    while (bytes != 0 && region < (uintptr_t)end)
    {
        *(volatile uint8_t*)(region < (uintptr_t)begin ? begin : (void*)region) = 0;
        region += BUSTER_ARENA_HUGE_PAGE_BYTES;
    }
}

// A lane's share of the pass: its items by their tokens.
static void check_deferred_pass_lane(void* argument)
{
    CheckDeferredPass* pass = argument;
    const Range64 items = lane_range_by_cost(pass->costs, (uint64_t)pass->deferred->body_count + 1);
    pass->warnings[lane_index()] = check_deferred_pass_items(pass, (uint32_t)items.begin, (uint32_t)items.end);
}

// Maps a decoded intrinsic's types through a part of the replay.
static void check_deferred_intrinsic_types(BuiltinIntrinsicTypeResult* decoded, const TypeReplayPart* part)
{
    decoded->function_type = check_deferred_type(part, decoded->function_type);
    decoded->result_type = check_deferred_type(part, decoded->result_type);
    for (uint32_t p = 0; p < decoded->parameter_count && p < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; p += 1)
    {
        decoded->parameters[p] = check_deferred_type(part, decoded->parameters[p]);
    }
}

// Places an intrinsic declaration (`record`, in `part`'s ids) at the next of `declarations`, of room for `capacity`,
// unless its catalogue entry has one (`places`: each entry's place + 1), and writes its place: whether it fit.
static bool check_deferred_intrinsic_place(const CheckInputIntrinsicDeclaration* record, const TypeReplayPart* part, uint32_t* places,
                                           CheckInputIntrinsicDeclaration* declarations, uint32_t capacity, uint32_t* total, uint32_t* place)
{
    const uint32_t entry = (uint32_t)(record->info - builtin_intrinsic_infos);
    const bool fresh = places[entry] == 0;
    const bool valid = !fresh || *total < capacity;
    if (fresh && valid)
    {
        declarations[*total] = *record;
        check_deferred_intrinsic_types(&declarations[*total].types, part);
        *total += 1;
        places[entry] = *total;
    }
    *place = valid ? places[entry] - 1 : 0;
    return valid;
}

// Merges every body's result into the top level's reader, in source order: whether everything fit and was replayed.
static bool check_deferred_merge(CheckDeferred* deferred, CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    Arena* scratch = &deferred->memory.arena;
    const uint32_t bodies = deferred->body_count;
    const uint32_t count = parser->tokens.count;
    const uint64_t begin_ns = deferred->timing ? check_deferred_now() : 0;
    // The types: the top level's changes and the bodies', replayed into the caller's table.
    TypeReplayPart* parts = BUSTER_ARENA_PUSH_ARRAY(scratch, TypeReplayPart, (uint64_t)bodies + 1);
    bool valid = parts != 0;
    if (valid)
    {
        parts[0] = (TypeReplayPart){
            .changes = &deferred->top_changes,
            .base = BUSTER_TYPE_BUILTIN_COUNT,
            .tag_base = 1,
            .types = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, deferred->top.types.count),
            .tags = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, deferred->top.tags.count),
            .merged = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, deferred->top.tags.count),
        };
        valid = parts[0].types != 0 && parts[0].tags != 0 && parts[0].merged != 0;
    }
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        const CheckDeferredResult* result = deferred->results + b;
        parts[1 + b] = (TypeReplayPart){
            .changes = &result->changes,
            .parent = &parts[0],
            .base = deferred->top.types.count,
            .tag_base = deferred->top.tags.count,
            .types = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)result->types + 1),
            .tags = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)result->tags + 1),
            .merged = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)result->tags + 1),
        };
        valid = parts[1 + b].types != 0 && parts[1 + b].tags != 0 && parts[1 + b].merged != 0;
    }
    valid = valid && type_replay(deferred->table, parts, bodies + 1, scratch) && !deferred->fail_after_replay;
    const uint64_t replay_ns = deferred->timing ? check_deferred_now() : 0;
    // Each appended table's places, and the events'.
    uint32_t* counts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1);
    valid = valid && counts != 0;
    const CheckInput top_input = *input;
    const uint32_t top_strings = input->string_count;
    const uint32_t top_regions = input->region_count;
    const uint32_t top_vlas = input->vla_count;
    const uint32_t top_asms = input->asm_count;
    const uint32_t top_varargs = reader->vararg_count;
    const uint32_t top_constants = reader->cached_constant_count;
    const uint32_t top_events = reader->bindings.count;
    uint32_t* string_map = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_strings + 1) : 0;
    uint32_t* region_map = string_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_regions + 1) : 0;
    uint32_t* vla_map = region_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_vlas + 1) : 0;
    uint32_t* asm_map = vla_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_asms + 1) : 0;
    uint32_t* vararg_map = asm_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_varargs + 1) : 0;
    uint32_t* constant_map = vararg_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_constants + 1) : 0;
    uint32_t* event_map = constant_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_events + 1) : 0;
    uint32_t* string_bases = event_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* region_bases = string_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* vla_bases = region_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* asm_bases = vla_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* vararg_bases = asm_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* constant_bases = vararg_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* event_bases = constant_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    valid = event_bases != 0;
    uint32_t string_total = 0;
    uint32_t region_total = 0;
    uint32_t vla_total = 0;
    uint32_t asm_total = 0;
    uint32_t vararg_total = 0;
    uint32_t constant_total = 0;
    uint32_t event_total = 0;
    if (valid)
    {
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].string_count;
        }
        string_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, strings), counts, top_strings, string_map, string_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].region_count;
        }
        region_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, regions), counts, top_regions, region_map, region_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].vla_count;
        }
        vla_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, vlas), counts, top_vlas, vla_map, vla_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].asm_count;
        }
        asm_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, asms), counts, top_asms, asm_map, asm_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].vararg_count;
        }
        vararg_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, varargs), counts, top_varargs, vararg_map, vararg_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].constant_count;
        }
        constant_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, constants), counts, top_constants, constant_map, constant_bases);
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            counts[b] = deferred->results[b].event_count;
        }
        event_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, events), counts, top_events, event_map, event_bases);
        valid = string_total <= count + 1 && region_total <= count + 1 && vla_total <= input->vla_capacity && asm_total <= input->asm_capacity && vararg_total <= reader->vararg_capacity &&
                event_total <= reader->bindings.capacity;
    }
    // The appended tables, merged into new arrays of the input's capacities.
    CheckInitString* strings = valid ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInitString, (uint64_t)count + 1) : 0;
    CheckInputRegion* regions = strings != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputRegion, (uint64_t)count + 1) : 0;
    CheckInputVla* vlas = regions != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputVla, (uint64_t)input->vla_capacity + 1) : 0;
    CheckInputAsm* asms = vlas != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAsm, (uint64_t)input->asm_capacity + 1) : 0;
    CheckInputVarargs* varargs = asms != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckInputVarargs, (uint64_t)reader->vararg_capacity + 1) : 0;
    CheckValue* constants = varargs != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, (uint64_t)constant_total + 1) : 0;
    valid = constants != 0;
    for (uint32_t i = 0; valid && i < top_strings; i += 1)
    {
        strings[string_map[i]] = input->strings[i];
        strings[string_map[i]].type = check_deferred_type(&parts[0], input->strings[i].type);
    }
    for (uint32_t i = 0; valid && i < top_regions; i += 1)
    {
        regions[region_map[i]] = input->regions[i];
    }
    for (uint32_t i = 0; valid && i < top_vlas; i += 1)
    {
        vlas[vla_map[i]] = input->vlas[i];
        vlas[vla_map[i]].type = check_deferred_type(&parts[0], input->vlas[i].type);
    }
    for (uint32_t i = 0; valid && i < top_asms; i += 1)
    {
        asms[asm_map[i]] = input->asm_statements[i];
    }
    for (uint32_t i = 0; valid && i < top_varargs; i += 1)
    {
        CheckInputVarargs record = reader->varargs[i];
        record.context.va_list_type = check_deferred_type(&parts[0], record.context.va_list_type);
        record.context.function_type = check_deferred_type(&parts[0], record.context.function_type);
        record.context.last_parameter_type = check_deferred_type(&parts[0], record.context.last_parameter_type);
        record.requested_type = check_deferred_type(&parts[0], record.requested_type);
        varargs[vararg_map[i]] = record;
    }
    for (uint32_t i = 0; valid && i < top_constants; i += 1)
    {
        constants[constant_map[i]] = reader->cached_constants[i];
        constants[constant_map[i]].type = check_deferred_type(&parts[0], reader->cached_constants[i].type);
    }
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        const CheckDeferredResult* result = deferred->results + b;
        const TypeReplayPart* part = &parts[1 + b];
        for (uint32_t i = 0; i < result->string_count; i += 1)
        {
            strings[string_bases[b] + i] = result->strings[i];
            strings[string_bases[b] + i].type = check_deferred_type(part, result->strings[i].type);
        }
        for (uint32_t i = 0; i < result->region_count; i += 1)
        {
            regions[region_bases[b] + i] = result->regions[i];
        }
        for (uint32_t i = 0; i < result->vla_count; i += 1)
        {
            vlas[vla_bases[b] + i] = result->vlas[i];
            vlas[vla_bases[b] + i].type = check_deferred_type(part, result->vlas[i].type);
        }
        for (uint32_t i = 0; i < result->changed_vla_count; i += 1)
        {
            CheckInputVla record = result->changed_vlas[i];
            record.type = check_deferred_type(&parts[0], record.type);
            vlas[vla_map[result->changed_vla_indices[i]]] = record;
        }
        for (uint32_t i = 0; i < result->asm_count; i += 1)
        {
            asms[asm_bases[b] + i] = result->asms[i];
        }
        for (uint32_t i = 0; i < result->vararg_count; i += 1)
        {
            CheckInputVarargs record = result->varargs[i];
            record.context.va_list_type = check_deferred_type(part, record.context.va_list_type);
            record.context.function_type = check_deferred_type(part, record.context.function_type);
            record.context.last_parameter_type = check_deferred_type(part, record.context.last_parameter_type);
            record.requested_type = check_deferred_type(part, record.requested_type);
            varargs[vararg_bases[b] + i] = record;
        }
        for (uint32_t i = 0; i < result->constant_count; i += 1)
        {
            constants[constant_bases[b] + i] = result->constants[i];
            constants[constant_bases[b] + i].type = check_deferred_type(part, result->constants[i].type);
        }
    }
    // The intrinsics, merged into new tables of the input's capacities: each declaration where source order's walk makes
    // it, the top level's up to each body's cut and then the body's own, each in its walk's order, a catalogue entry's
    // first kept and its others mapped to it, as source order makes a name's once (an entry has one name); and the calls
    // at their cuts, each with its declaration's place.
    const bool intrinsics = input->intrinsic_ids != 0;
    const uint32_t top_declarations = input->intrinsic_declaration_count;
    const uint32_t top_calls = input->intrinsic_call_count;
    uint32_t* call_map = valid && intrinsics ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_calls + 1) : 0;
    uint32_t* call_bases = call_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    uint32_t* declaration_bases = call_bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)bodies + 1) : 0;
    valid = valid && (!intrinsics || declaration_bases != 0);
    uint32_t body_declarations = 0;
    for (uint32_t b = 0; valid && intrinsics && b < bodies; b += 1)
    {
        declaration_bases[b] = top_declarations + body_declarations;
        body_declarations += deferred->results[b].intrinsic_declaration_count;
    }
    uint32_t* declaration_map = valid && intrinsics ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)top_declarations + body_declarations + 1) : 0;
    uint32_t* entry_places = declaration_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)BUSTER_BUILTIN_INTRINSIC_COUNT + 1) : 0;
    CheckInputIntrinsicDeclaration* intrinsic_declarations =
        entry_places != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputIntrinsicDeclaration, (uint64_t)input->intrinsic_declaration_capacity + 1) : 0;
    CheckInputIntrinsicCall* intrinsic_calls =
        intrinsic_declarations != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputIntrinsicCall, (uint64_t)input->intrinsic_call_capacity + 1) : 0;
    valid = valid && (!intrinsics || intrinsic_calls != 0);
    uint32_t declaration_total = 0;
    uint32_t call_total = 0;
    if (valid && intrinsics)
    {
        // A catalogue entry's place + 1, 0 while it has none.
        memset(entry_places, 0, ((uint64_t)BUSTER_BUILTIN_INTRINSIC_COUNT + 1) * sizeof(uint32_t));
        const uint32_t capacity = input->intrinsic_declaration_capacity;
        uint32_t t = 0;
        for (uint32_t b = 0; valid && b <= bodies; b += 1)
        {
            // The top level's before the body's cut, then the body's.
            const uint32_t until = b < bodies ? deferred->cuts[b].intrinsic_declarations : top_declarations;
            for (; valid && t < until; t += 1)
            {
                valid = check_deferred_intrinsic_place(input->intrinsic_declarations + t, &parts[0], entry_places, intrinsic_declarations, capacity, &declaration_total,
                                                       declaration_map + t);
            }
            const CheckDeferredResult* result = b < bodies ? deferred->results + b : 0;
            for (uint32_t j = 0; valid && result != 0 && j < result->intrinsic_declaration_count; j += 1)
            {
                valid = check_deferred_intrinsic_place(result->intrinsic_declarations + j, &parts[1 + b], entry_places, intrinsic_declarations, capacity,
                                                       &declaration_total, declaration_map + declaration_bases[b] + j);
            }
        }
        for (uint32_t c = 0; c < bodies; c += 1)
        {
            counts[c] = deferred->results[c].intrinsic_call_count;
        }
        call_total = check_deferred_layout(deferred, offsetof(CheckDeferredCut, intrinsic_calls), counts, top_calls, call_map, call_bases);
        valid = valid && call_total <= input->intrinsic_call_capacity;
        for (uint32_t c = 0; valid && c < top_calls; c += 1)
        {
            CheckInputIntrinsicCall call = input->intrinsic_calls[c];
            call.declaration = declaration_map[call.declaration];
            intrinsic_calls[call_map[c]] = call;
        }
        for (uint32_t body = 0; valid && body < bodies; body += 1)
        {
            const CheckDeferredResult* result = deferred->results + body;
            for (uint32_t c = 0; c < result->intrinsic_call_count; c += 1)
            {
                CheckInputIntrinsicCall call = result->intrinsic_calls[c];
                const uint32_t declaration = call.declaration;
                call.declaration = declaration_map[declaration < top_declarations ? declaration : declaration_bases[body] + (declaration - top_declarations)];
                intrinsic_calls[call_bases[body] + c] = call;
            }
        }
    }
    const uint64_t tables_ns = deferred->timing ? check_deferred_now() : 0;
    // The events: the merged stream's columns, of the stream's capacity, which the pass fills (check_deferred_pass_events),
    // and the top level's active declarations and every name's head, which index into the stream, mapped.
    CBindings* bindings = &reader->bindings;
    const uint64_t capacity = (uint64_t)bindings->capacity + 1;
    uint32_t* key = valid ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint32_t* declaration = key != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint32_t* point = declaration != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint32_t* end = point != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint32_t* previous = end != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint32_t* depth = previous != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity) : 0;
    uint8_t* operation = depth != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, capacity) : 0;
    uint8_t* kind = operation != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, capacity) : 0;
    valid = kind != 0;
    if (valid)
    {
        for (uint32_t i = 0; i < bindings->active_count; i += 1)
        {
            bindings->active[i] = event_map[bindings->active[i]];
        }
        const uint64_t keys = (uint64_t)bindings->name_capacity * C_BINDING_NAMESPACES;
        for (uint64_t k = 0; k < keys; k += 1)
        {
            bindings->heads[k] = bindings->heads[k] != 0 ? event_map[bindings->heads[k] - 1] + 1 : 0;
        }
    }
    const uint64_t events_ns = deferred->timing ? check_deferred_now() : 0;
    // The tags' completion positions, by the replay's tags.
    uint32_t* completions = valid ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)deferred->table->tags.capacity + 1) : 0;
    uint32_t* transparent = valid && reader->transparent_unions != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)deferred->table->tags.capacity + 1) : 0;
    valid = completions != 0 && (reader->transparent_unions == 0 || transparent != 0);
    if (valid)
    {
        // As the reader starts them: never complete, and not transparent.
        memset(completions, 0xff, ((uint64_t)deferred->table->tags.capacity + 1) * sizeof(uint32_t));
        if (transparent != 0)
        {
            memset(transparent, 0, ((uint64_t)deferred->table->tags.capacity + 1) * sizeof(uint32_t));
        }
        for (uint32_t t = 1; t < deferred->top.tags.count; t += 1)
        {
            completions[type_replay_tag(&parts[0], t)] = reader->tag_completions[t];
            if (transparent != 0)
            {
                transparent[type_replay_tag(&parts[0], t)] = reader->transparent_unions[t];
            }
        }
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            const CheckDeferredResult* result = deferred->results + b;
            for (uint32_t t = 0; t < result->tags; t += 1)
            {
                // A body's predefined tag merged into an earlier one keeps that one's position.
                if (parts[1 + b].merged[t] == 0)
                {
                    completions[type_replay_tag(&parts[1 + b], deferred->top.tags.count + t)] = result->tag_completions[t];
                    // And a union the body made transparent, its activation.
                    if (transparent != 0)
                    {
                        transparent[type_replay_tag(&parts[1 + b], deferred->top.tags.count + t)] = result->transparent[t];
                    }
                }
            }
            // The top level's tags the body completed: the top level's walk left them incomplete.
            for (uint32_t c = 0; c < result->completed_count; c += 1)
            {
                completions[type_replay_tag(&parts[0], result->completed_tags[c])] = result->completed_positions[c];
            }
        }
    }
    const uint64_t tags_ns = deferred->timing ? check_deferred_now() : 0;
    // The pass over the events and every column the walk wrote with a TypeId or an id into those tables, on the lanes
    // that read the bodies, each a share of the items by their tokens (check_deferred_pass_items).
    uint32_t* positions = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint64_t* costs = positions != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)bodies + 1) : 0;
    valid = valid && costs != 0;
    CheckDeferredPass pass = {
        .deferred = deferred,
        .reader = reader,
        .parts = parts,
        .costs = costs,
        .positions = positions,
        .vector = !BUSTER_SCALAR_PATH(parser->options.scalar) && !deferred->scalar_merge,
        .event_map = event_map,
        .event_bases = event_bases,
        .top_events = top_events,
        .key = key,
        .declaration = declaration,
        .point = point,
        .end = end,
        .previous = previous,
        .depth = depth,
        .operation = operation,
        .kind = kind,
        .types = {
            input->declared_types,
            reader->query_types,
            reader->return_types,
            reader->incomplete_array_types,
            reader->selection_types,
            reader->tag_types,
            reader->declaration_bases,
        },
    };
    // The id columns of the tables some body appended to: a table no body appended to maps each id to itself, and its
    // column stays as it is.
    const struct
    {
        CheckDeferredIds ids;
        uint32_t total;
    } id_columns[] = {
        { { reader->string_ids, BUSTER_CHECK_NONE, 0, string_map, string_bases, top_strings }, string_total },
        { { input->expression_regions, 0, 1, region_map, region_bases, top_regions }, region_total },
        { { reader->vla_bounds, 0, 1, vla_map, vla_bases, top_vlas }, vla_total },
        { { reader->vla_ends, 0, 1, vla_map, vla_bases, top_vlas }, vla_total },
        { { input->asm_ids, 0, 1, asm_map, asm_bases, top_asms }, asm_total },
        { { input->intrinsic_ids, 0, 1, call_map, call_bases, top_calls }, call_total },
        { { reader->vararg_calls, 0, 1, vararg_map, vararg_bases, top_varargs }, vararg_total },
        { { reader->vararg_types, 0, 1, vararg_map, vararg_bases, top_varargs }, vararg_total },
        { { reader->constant_present, 0, 1, constant_map, constant_bases, top_constants }, constant_total },
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(id_columns) == BUSTER_ARRAY_LENGTH(pass.ids), "every id column has room in the pass");
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(id_columns); c += 1)
    {
        if (id_columns[c].ids.column != NULL && id_columns[c].total != id_columns[c].ids.top)
        {
            pass.ids[pass.id_count] = id_columns[c].ids;
            pass.id_count += 1;
        }
    }
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        costs[b] = (uint64_t)deferred->cuts[b].closing + 1;
    }
    if (valid)
    {
        costs[bodies] = (uint64_t)count + 1;
    }
    // On one lane, this thread; on more, a lane_run of the lanes the bodies ran on, which are awake. On more than
    // one lane, fault in the columns the pass writes first, on this thread (check_deferred_fault_range): the merged
    // stream's events, every one of which the pass writes, and the token positions, which its scans fill from their
    // start. On one lane this thread writes them alone, and needs no fault-in.
    const uint32_t lanes = deferred->active > 1 ? deferred->active : 1;
    if (valid && lanes > 1)
    {
        check_deferred_fault_range(key, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(declaration, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(point, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(end, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(previous, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(depth, (uint64_t)event_total * sizeof(uint32_t));
        check_deferred_fault_range(operation, (uint64_t)event_total);
        check_deferred_fault_range(kind, (uint64_t)event_total);
        check_deferred_fault_range(positions, ((uint64_t)count + 1) * sizeof(uint32_t));
    }
    if (valid && lanes == 1)
    {
        input->constant_warning_count = check_deferred_pass_items(&pass, 0, bodies + 1);
    }
    else if (valid)
    {
        valid = lane_run(lanes, check_deferred_pass_lane, &pass, deferred->cpus);
        deferred->reason = !valid ? "the merge's lanes couldn't start" : deferred->reason;
        input->constant_warning_count = 0;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            input->constant_warning_count += pass.warnings[lane];
        }
    }
    input->constant_warnings_prepared = false;
    if (valid)
    {
        bindings->key = key;
        bindings->declaration = declaration;
        bindings->point = point;
        bindings->end = end;
        bindings->previous = previous;
        bindings->depth = depth;
        bindings->operation = operation;
        bindings->kind = kind;
        bindings->count = event_total;
    }
    const uint64_t pass_ns = deferred->timing ? check_deferred_now() : 0;
    for (uint32_t name = 0; valid && name < reader->bindings.name_capacity; name += 1)
    {
        reader->linked_types[name] = check_deferred_type(&parts[0], reader->linked_types[name]);
    }
    for (uint32_t i = 0; valid && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        // Each predefined type is the first maker's by position: a body's before the top level's.
        TypeId made = reader->predefined_types[i] != 0 ? check_deferred_type(&parts[0], reader->predefined_types[i]) : 0;
        uint32_t position = reader->predefined_positions[i];
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            const CheckDeferredResult* result = deferred->results + b;
            const bool earlier = result->predefined[i] != 0 && deferred->cuts[b].opening < position;
            made = earlier ? check_deferred_type(&parts[1 + b], result->predefined[i]) : made;
            position = earlier ? deferred->cuts[b].opening : position;
        }
        reader->predefined_types[i] = made;
        reader->predefined_positions[i] = position;
    }
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        reader->file_function_name_ids[i] = reader->file_function_name_ids[i] != BUSTER_CHECK_NONE ? string_map[reader->file_function_name_ids[i]] : BUSTER_CHECK_NONE;
    }
    CheckConstBuffer const_values = { .arena = reader->arena };
    valid = valid && check_deferred_const_values(&const_values, &top_input.const_values, &parts[0], reader->scratch);
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        valid = check_deferred_const_values(&const_values, &deferred->results[b].const_values, &parts[1 + b], reader->scratch);
    }
    CheckWarningBuffer warnings = { .arena = reader->arena };
    valid = valid && check_deferred_warnings(&warnings, &top_input.warning_events, &parts[0], reader->scratch);
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        valid = check_deferred_warnings(&warnings, &deferred->results[b].warning_events, &parts[1 + b], reader->scratch);
    }
    if (valid)
    {
        if (intrinsics)
        {
            input->intrinsic_declarations = intrinsic_declarations;
            input->intrinsic_declaration_count = declaration_total;
            input->intrinsic_calls = intrinsic_calls;
            input->intrinsic_call_count = call_total;
        }
        reader->warning_events = warnings;
        reader->const_values = const_values;
        input->const_values = check_const_view(&const_values);
        input->warning_events = check_warning_view(&warnings);
        input->constant_warnings_prepared = true;
        // Literal tables belong to the top-level reader; their retained array type must follow the same replay as
        // declared_types, particularly for tables after bodies that insert new types into the merged table.
        for (CheckInputLiteralTable* table = input->literal_tables; table != NULL && valid; table = table->next)
        {
            const TypeId type = check_deferred_type(&parts[0], table->plan.type);
            valid = type != 0;
            table->plan.type = type;
            table->part.type = type;
        }
        input->strings = strings;
        input->string_count = string_total;
        input->regions = regions;
        input->region_count = region_total;
        input->vlas = vlas;
        input->vla_count = vla_total;
        input->asm_statements = asms;
        input->asm_count = asm_total;
        reader->varargs = varargs;
        reader->vararg_count = vararg_total;
        reader->cached_constants = constants;
        reader->cached_constant_count = constant_total;
        reader->tag_completions = completions;
        reader->transparent_unions = transparent != 0 ? transparent : reader->transparent_unions;
        reader->types = deferred->table;
        reader->function_body = BUSTER_CHECK_NONE;
        reader->function_result = 0;
        // What the last body to leave it behind left: source order's reader ends with it.
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            const CheckDeferredResult* result = deferred->results + b;
            reader->inference_end = result->inference_end > reader->inference_end ? result->inference_end : reader->inference_end;
            if (result->function_name_body != BUSTER_CHECK_NONE)
            {
                reader->function_name_body = result->function_name_body;
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    const uint32_t id = result->function_name_ids[i];
                    reader->function_name_ids[i] = id != BUSTER_CHECK_NONE ? check_deferred_id(string_map, top_strings, string_bases[b], id) : BUSTER_CHECK_NONE;
                }
            }
        }
        // As source order's input: no body is deferred.
        input->deferred_tokens = 0;
    }
    if (deferred->timing)
    {
        deferred->merge_replay_ns = replay_ns - begin_ns;
        deferred->merge_tables_ns = tables_ns - replay_ns;
        deferred->merge_events_ns = events_ns - tables_ns;
        deferred->merge_tags_ns = tags_ns - events_ns;
        deferred->merge_pass_ns = pass_ns - tags_ns;
        deferred->merge_rest_ns = check_deferred_now() - pass_ns;
    }
    return valid;
}

// The first lane must read the largest body, as the critical path assumes. The costliest aggregate part need not
// contain it: two smaller bodies can cost more. Promote its part stably, leaving the others in their cost order.
static void check_deferred_primary_part(CheckDeferred* deferred)
{
    for (uint64_t taken = 0; taken < deferred->parts; taken += 1)
    {
        const uint32_t part = deferred->order[taken];
        if (deferred->boundaries[part] <= deferred->largest_index && deferred->largest_index < deferred->boundaries[part + 1])
        {
            if (taken != 0)
            {
                memmove(deferred->order + 1, deferred->order, taken * sizeof(*deferred->order));
                deferred->order[0] = part;
            }
            break;
        }
    }
}

// The lanes the bodies take: one, and one more per `lane_tokens` of the bodies' tokens (`total` of them in `bodies`)
// beyond their critical path, the larger of the largest body and twice the second largest, no more than the reader's
// lanes or the bodies (check_deferred_bodies says why).
static uint32_t check_deferred_rule(const CheckDeferred* deferred, uint64_t total, uint32_t bodies, uint32_t largest, uint32_t second_body, uint32_t lane_tokens)
{
    const uint64_t second = (uint64_t)second_body * 2;
    const uint64_t path = largest > second ? largest : second;
    const uint64_t paid = deferred->every_lane ? UINT64_MAX : 1 + (total > path ? total - path : 0) / lane_tokens;
    const uint32_t most = deferred->lanes < bodies ? deferred->lanes : bodies;
    return paid < most ? (uint32_t)paid : most;
}

// Whether check_input_run's arena of rare columns is base pages throughout: where the rule gives a large unit's bodies
// more than one lane, which write their entries of the token columns at once (#764, #847).
static bool check_deferred_shared(const CheckDeferred* deferred)
{
    return deferred->planned > 1 && deferred->large;
}


static void check_deferred_bodies(CheckDeferred* deferred, CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    deferred->top.changes = 0;
    // A unit whose top level reported, that has overloadable functions, or that has #pragma clang section, whose settings
    // and conflicts read the walk's declarations in source order (c_check_section_input.c), is source order's.
    bool valid = !deferred->unsupported && input->report_count == 0 && !input->resource && reader->bindings.valid && !deferred->top_changes.full &&
                 input->overload_heads == 0 && input->sections == 0 && deferred->lanes != 0 && deferred->lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES;
    deferred->reason = input->report_count != 0 ? "the top level reports an error" : input->overload_heads != 0 ? "overloadable functions" :
                       input->sections != 0 ? "#pragma clang section" : !valid ? "the top level ran out of room" : 0;
    deferred->top_reader = reader;
    const uint64_t environment_begin = deferred->timing ? check_deferred_now() : 0;
    deferred->top_ns = deferred->timing ? environment_begin - deferred->top_ns : 0;
    valid = valid && c_environment_build(&deferred->environment, &reader->bindings, &deferred->memory.arena, reader->scratch);
    const uint64_t environment_end = deferred->timing ? check_deferred_now() : 0;
    deferred->environment_ns = environment_end - environment_begin;
    // The top level's linked updates by name, each name's in position order, as the walk logged them: a counting sort.
    const uint32_t names = reader->bindings.name_capacity;
    deferred->linked_offsets = valid && deferred->linked_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, (uint64_t)names + 2) : 0;
    deferred->linked_names = deferred->linked_offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredLinked, deferred->linked_count) : 0;
    valid = valid && (deferred->linked_count == 0 || deferred->linked_names != 0);
    if (valid && deferred->linked_count != 0)
    {
        uint32_t* offsets = deferred->linked_offsets;
        memset(offsets, 0, ((uint64_t)names + 2) * sizeof(uint32_t));
        for (uint32_t i = 0; i < deferred->linked_count; i += 1)
        {
            offsets[deferred->linked[i].name + 2] += 1;
        }
        for (uint32_t name = 0; name < names; name += 1)
        {
            offsets[name + 2] += offsets[name + 1];
        }
        // Each record at its name's next place: offsets[name + 1] ends as the name's end, the next name's start.
        for (uint32_t i = 0; i < deferred->linked_count; i += 1)
        {
            const uint32_t name = deferred->linked[i].name;
            deferred->linked_names[offsets[name + 1]] = deferred->linked[i];
            offsets[name + 1] += 1;
        }
    }
    // The top level's completions and transparency, in position order, which bodies before them withdraw.
    deferred->withdrawals = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, (uint64_t)deferred->top_changes.count + 1) : 0;
    deferred->transparencies = deferred->withdrawals != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, (uint64_t)deferred->top.tags.count + 1) : 0;
    valid = valid && deferred->transparencies != 0;
    for (uint32_t tag = 0; valid && tag < deferred->top.tags.count; tag += 1)
    {
        deferred->transparencies[tag] = BUSTER_CHECK_NONE;
    }
    for (uint32_t i = 0; valid && i < deferred->top_changes.count; i += 1)
    {
        const TypeChange* change = deferred->top_changes.items + i;
        const bool completes = change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM ||
                               change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT;
        if (change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT)
        {
            deferred->transparencies[change->tag] = deferred->withdrawal_count;
        }
        deferred->withdrawals[deferred->withdrawal_count] = i;
        deferred->withdrawal_count += completes;
    }
    // The bodies' costs, and the lanes' parts.
    deferred->costs = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint64_t, (uint64_t)deferred->body_count + 1) : 0;
    deferred->boundaries = deferred->costs != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint64_t, (uint64_t)BUSTER_LANE_MAX_PARTS + 1) : 0;
    deferred->results = deferred->boundaries != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredResult, (uint64_t)deferred->body_count + 1) : 0;
    deferred->order = deferred->results != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, (uint64_t)BUSTER_LANE_MAX_PARTS + 1) : 0;
    valid = valid && deferred->order != 0;
    uint64_t total = 0;
    for (uint32_t b = 0; valid && b < deferred->body_count; b += 1)
    {
        const CheckDeferredCut* cut = deferred->cuts + b;
        const uint32_t tokens = cut->closing - cut->opening + 1;
        total += tokens;
        deferred->costs[b] = total;
        deferred->results[b] = (CheckDeferredResult){ .unsupported = true };
        uint32_t parameters = 0;
        for (uint32_t p = reader->parameters[cut->opening]; p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
        {
            parameters += 1;
        }
        const bool larger = tokens > deferred->largest_body;
        deferred->second_body = larger ? deferred->largest_body : tokens > deferred->second_body ? tokens : deferred->second_body;
        deferred->largest_index = larger ? b : deferred->largest_index;
        deferred->largest_body = larger ? tokens : deferred->largest_body;
        deferred->largest_parameters = parameters > deferred->largest_parameters ? parameters : deferred->largest_parameters;
    }
    CheckDeferredRun run = { .deferred = deferred };
    // One lane, and one more per `lane_tokens` of the bodies' tokens beyond their critical path: the larger of the
    // largest body, which the first lane reads itself, and twice the second largest, which a woken lane reads at about
    // half the first lane's speed. A lane costs its copy of the reader and its wake-up, which its share of the walks must
    // pay for, and the copy grows with the top level: BUSTER_CHECK_DEFERRED_LANE_TOKENS, and one more per
    // BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS of the top level's tokens. And no more lanes than bodies, which are at least
    // as many as the parts: a lane past them would take none.
    const uint32_t count = reader->parser->tokens.count;
    deferred->top_tokens = count > total ? (uint32_t)(count - total) : 0;
    deferred->lane_tokens = BUSTER_CHECK_DEFERRED_LANE_TOKENS + deferred->top_tokens / BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS;
    const uint32_t active = check_deferred_rule(deferred, total, deferred->body_count, deferred->largest_body, deferred->second_body, deferred->lane_tokens);
    // The lanes' parts, cut here rather than by the lanes, which would wait at lane_parts_by_cost's sync for the last of
    // them to wake: the first lane, this thread, starts its part at once (check_deferred_lane).
    deferred->parts = valid && deferred->body_count != 0 ? parts_by_cost(deferred->costs, deferred->body_count, active, deferred->boundaries, deferred->order) : 0;
    check_deferred_primary_part(deferred);
    // Where more than one lane will write the rare columns and they are huge pages, a claim word per 2 MB region of their
    // token columns, which the lanes' readers take from this one's (check_input_rare_claim).
    const uint64_t regions = reader->rare_end > reader->rare_begin ? (((uintptr_t)reader->rare_end - 1) >> 21) - ((uintptr_t)reader->rare_begin >> 21) + 1 : 0;
    uint32_t* const claims = valid && active > 1 && !input->rare_columns_shared && regions != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint32_t, regions) : 0;
    if (claims != 0)
    {
        memset(claims, 0, regions * sizeof(uint32_t));
    }
    reader->rare_claims = claims;
    deferred->claiming = claims != 0;
    // The lanes' arenas, on this thread: those of a lane the state hasn't run reserved, the others taken back. A lane
    // without them takes its parts' bodies for source order's (check_deferred_lane_prepare).
    const uint64_t arenas_begin = deferred->timing ? check_deferred_now() : 0;
    for (uint32_t lane = 0; valid && lane < active; lane += 1)
    {
        check_deferred_lane_arenas(deferred->memory.lanes + lane);
    }
    const uint64_t lanes_begin = deferred->timing ? check_deferred_now() : 0;
    deferred->arenas_ns = lanes_begin - arenas_begin;
    // A one-lane cold run starts no worker and preserves this thread's affinity and context, independently of any
    // installed team. More than one lane keeps the team's fixed workers and CPU assignment.
    const bool ran = !valid || deferred->body_count == 0 ||
                     (active == 1 ? lane_run_cold(1, check_deferred_lane, &run, 0) : lane_run(active, check_deferred_lane, &run, deferred->cpus));
    deferred->reason = valid && !ran ? "the lanes couldn't start" : deferred->reason;
    valid = valid && ran;
    deferred->lanes_ns = deferred->timing ? check_deferred_now() - lanes_begin : 0;
    reader->rare_claims = 0;
    for (uint64_t region = 0; claims != 0 && region < regions; region += 1)
    {
        deferred->claimed += claims[region] == 2;
    }
    deferred->largest_lane = valid && deferred->body_count != 0 ? deferred->results[deferred->largest_index].lane : 0;
    // The first lane arrives after lane_run's dispatch, and the last one after the others' wake-up; a lane that didn't
    // run arrived nowhere (0).
    const uint64_t first_arrival = deferred->lane_states[0].arrived_ns;
    uint64_t last_arrival = first_arrival;
    for (uint32_t lane = 0; lane < deferred->lanes && lane < BUSTER_CHECK_DEFERRED_MAX_LANES; lane += 1)
    {
        const CheckDeferredLane* state = deferred->lane_states + lane;
        last_arrival = state->arrived_ns > last_arrival ? state->arrived_ns : last_arrival;
        deferred->bodies += state->bodies;
        deferred->body_tokens += state->body_tokens;
        deferred->history_reads += state->history_reads;
        deferred->withdrawn += state->withdrawn;
        deferred->copies += state->copied;
        deferred->position_ns += state->position_ns;
        deferred->walk_ns += state->walk_ns;
        deferred->copy_ns += state->copy_ns;
    }
    deferred->dispatch_ns = first_arrival != 0 ? first_arrival - lanes_begin : 0;
    deferred->wake_ns = first_arrival != 0 ? last_arrival - first_arrival : 0;
    for (uint32_t b = 0; valid && b < deferred->body_count; b += 1)
    {
        valid = !deferred->results[b].unsupported;
        deferred->reason = !valid ? deferred->results[b].reason : deferred->reason;
    }
    const uint64_t merge_begin = deferred->timing ? check_deferred_now() : 0;
    const bool merged = valid && check_deferred_merge(deferred, reader);
    deferred->merge_ns = deferred->timing ? check_deferred_now() - merge_begin : 0;
    deferred->reason = valid && !merged && deferred->reason == 0 ? "the merge ran out of room" : deferred->reason;
    valid = merged;
    // A unit that isn't merged is source order's: nothing more of this run counts.
    deferred->unsupported |= !valid;
    input->resource |= !valid;
}

// The state's memory, its lanes' among it, which its readings' inputs point into: release it once the last input is no
// longer used. The state then reserves memory again if it reads another unit.
static void check_deferred_release(CheckDeferred* deferred)
{
    CheckDeferredMemory* memory = &deferred->memory;
    for (uint32_t lane = 0; lane < BUSTER_CHECK_DEFERRED_MAX_LANES; lane += 1)
    {
        CheckDeferredArenas* arenas = memory->lanes + lane;
        if (arenas->mapping != 0)
        {
            munmap(arenas->mapping, arenas->mapping_bytes);
        }
        *arenas = (CheckDeferredArenas){ 0 };
    }
    arena_release(&memory->top);
    arena_release(&memory->top_index);
    arena_release(&memory->arena);
}

// For the tests (test-checker-deferred): every lane's last body's stamp set to `stamp`, so that UINT32_MAX makes each
// lane's stamps wrap at its next body (check_deferred_heads).
static __attribute__((unused)) void check_deferred_stamp(CheckDeferred* deferred, uint32_t stamp)
{
    for (uint32_t lane = 0; lane < BUSTER_CHECK_DEFERRED_MAX_LANES; lane += 1)
    {
        deferred->memory.lanes[lane].stamp = stamp;
    }
}

// For the tests (test-checker-deferred): every byte the state's last reading left pushed in its memory set to `byte`,
// as a later reading of the state finds what earlier ones wrote, which it must not read as zeros.
static __attribute__((unused)) void check_deferred_poison(CheckDeferred* deferred, uint8_t byte)
{
    CheckDeferredMemory* memory = &deferred->memory;
    Arena* own[] = { &memory->arena, &memory->top, &memory->top_index };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(own); i += 1)
    {
        if (own[i]->base != 0)
        {
            memset(own[i]->base, byte, own[i]->length);
        }
    }
    for (uint32_t lane = 0; lane < BUSTER_CHECK_DEFERRED_MAX_LANES; lane += 1)
    {
        CheckDeferredArenas* arenas = memory->lanes + lane;
        Arena* list[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { &arenas->output, &arenas->scratch, &arenas->results, &arenas->types, &arenas->type_index, &arenas->names };
        for (uint32_t i = 0; arenas->mapping != 0 && i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
        {
            memset(list[i]->base, byte, list[i]->length);
        }
        // The per-name columns that a reading leaves as none hold the poison too, which the next reading clears
        // (check_deferred_lane_names).
        arenas->names_dirty |= arenas->mapping != 0 && arenas->names.length != 0;
    }
}

// check_input with every function body read apart, on `lanes` lanes, through `binding_mode`'s resolution. `deferred` is
// the reader's state, which starts zeroed and which the caller keeps until it releases it (check_deferred_release); its
// `unsupported` says that the unit was checked in source order instead. The result is check_input's in either case. A
// state may read several units, one after another: each reading takes back the memory the state's earlier readings
// reserved, and with it their inputs, which point into it.
static bool check_input_deferred(CheckDeferred* deferred, CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena,
                                 Arena* scratch, uint32_t lanes, CheckInputBindingMode binding_mode)
{
    const bool timing = deferred->timing;
    const uint32_t* cpus = deferred->cpus;
    const bool fail_after_replay = deferred->fail_after_replay;
    const bool hold_lanes = deferred->hold_lanes;
    const bool late_first = deferred->late_first;
    const bool every_lane = deferred->every_lane;
    const bool scalar_merge = deferred->scalar_merge;
    const uint32_t base_page_tokens = deferred->base_page_tokens;
    const CheckDeferredMemory memory = deferred->memory;
    *deferred = (CheckDeferred){
        .memory = memory,
        .lanes = lanes,
        .table = types,
        .timing = timing,
        .cpus = cpus,
        .every_lane = every_lane,
        .scalar_merge = scalar_merge,
        .base_page_tokens = base_page_tokens,
        .fail_after_replay = fail_after_replay,
        .hold_lanes = hold_lanes,
        .late_first = late_first,
    };
    const uint32_t count = parser->tokens.count;
    const uint64_t arena_mark = arena->length;
    // Plain char's signedness on the caller's table, which the merge fills and the phases after the reader read
    // (check_input_run sets the top level's, and type_copy each lane's).
    types->char_unsigned = pp->options.predefines.char_unsigned;
    // The caller's table as it comes, which a fallback to source order takes it back to: the merge replays into it, and
    // may fail after that.
    const TypeViewMark empty = type_view_mark(types);
    const TypeError empty_error = types->last_error;
    // The state's own arena, and address space for the top level's table and its record at the caller's capacities:
    // pages are committed as they are touched, and stay for the state's later readings.
    check_deferred_arena(&deferred->memory.arena, UINT64_C(1) << 36);
    check_deferred_arena(&deferred->memory.top, UINT64_C(1) << 40);
    check_deferred_arena(&deferred->memory.top_index, UINT64_C(1) << 34);
    // Every function body at file scope deferred: a run from its opening brace to its closing one.
    uint8_t* mask = deferred->memory.arena.base != 0 && parser->tree.roles != 0 ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, uint8_t, (uint64_t)count + 1) : 0;
    bool valid = mask != 0 && types->types.count == BUSTER_TYPE_BUILTIN_COUNT && types->tags.count == 1 && types->changes == 0 && lanes != 0 &&
                 lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES;
    uint32_t bodies = 0;
    uint64_t body_tokens = 0;
    uint32_t largest = 0;
    uint32_t second = 0;
    if (valid)
    {
        memset(mask, 0, (uint64_t)count + 1);
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t close = parser->tree.parents[token];
            if (parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY && close > token && close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_BODY)
            {
                const uint32_t tokens = close - token + 1;
                memset(mask + token, 1, tokens);
                bodies += 1;
                body_tokens += tokens;
                second = tokens > largest ? largest : tokens > second ? tokens : second;
                largest = tokens > largest ? tokens : largest;
                token = close;
            }
        }
    }
    // The lanes the rule will give the bodies, from the counts check_deferred_bodies takes again from the top level's
    // cuts, before check_input_run reserves its arena of rare columns by them and the unit's size (check_deferred_shared).
    const uint32_t top_tokens = count > body_tokens ? (uint32_t)(count - body_tokens) : 0;
    deferred->large = count >= (deferred->base_page_tokens != 0 ? deferred->base_page_tokens : BUSTER_CHECK_DEFERRED_BASE_PAGE_TOKENS);
    deferred->planned = check_deferred_rule(deferred, body_tokens, bodies, largest, second,
                                            BUSTER_CHECK_DEFERRED_LANE_TOKENS + top_tokens / BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS);
    deferred->cuts = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredCut, (uint64_t)bodies + 1) : 0;
    deferred->body_capacity = bodies;
    valid = valid && deferred->cuts != 0;
    // Room for the top level's linked updates (check_deferred_linked): two a token at most, a declarator's and its
    // initializer's. Pages are committed as the log fills them.
    const uint64_t linked_room = (uint64_t)count * 2 + 1;
    deferred->linked = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredLinked, linked_room) : 0;
    deferred->linked_capacity = linked_room < UINT32_MAX ? (uint32_t)linked_room : UINT32_MAX;
    valid = valid && deferred->linked != 0;
    // The top level's table, with the caller's capacities, and its record of changes.
    const TypeCapacity capacity = {
        .types = types->types.capacity - BUSTER_TYPE_BUILTIN_COUNT,
        .parameters = types->parameters.capacity,
        .tags = types->tags.capacity - 1,
        .members = types->members.capacity - 1,
        .lookups = types->lookup.capacity - 1,
    };
    valid = valid && type_initialize(&deferred->top, &deferred->memory.top, &deferred->memory.top_index, capacity) &&
            type_changes_initialize(&deferred->top_changes, &deferred->memory.top, types->types.capacity + types->tags.capacity * 4, types->parameters.capacity,
                                    types->members.capacity);
    deferred->top.changes = valid ? &deferred->top_changes : 0;
    deferred->reason = !valid ? "the deferred reader's own memory" : 0;
    // check_deferred_bodies ends the top level's span, which starts here.
    deferred->top_ns = timing ? check_deferred_now() : 0;
    bool checked = valid && check_input_run(input, parser, pp, &deferred->top, arena, scratch, true, count, mask, binding_mode, deferred, true, true, false);
    deferred->top.changes = 0;
    deferred->unsupported |= !valid;
    deferred->reason = deferred->unsupported && deferred->reason == 0 ? "the top level's walk stopped" : deferred->reason;
    if (deferred->unsupported)
    {
        // Source order, from nothing: the caller's table back to how it came (a merge that failed after its replay
        // filled it), and the input's records gone.
        const bool reset = type_view_reset(types, empty);
        types->last_error = empty_error;
        arena_pop_to(arena, arena_mark);
        checked = reset && check_input_bindings_mode(input, parser, pp, types, arena, scratch, binding_mode);
        input->resource |= !reset;
    }
    return checked;
}
