// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #113's deferred bodies. check_input_deferred checks a unit as check_input does, with the same CheckInput and TypeTable,
// but reads every function body apart from the top level, out of order and on any number of lanes:
//   - The top level is read first: check_input_run with every function body deferred, on a table of its own that records
//     its changes (TypeChanges). Where the walk skips a body, check_deferred_cut records the reader's counts there: its
//     types and tags, its binding events and active declarations, and each table the walk appends to.
//   - After the walk, check_deferred_bodies reads the bodies on lanes: one, and one more per
//     BUSTER_CHECK_DEFERRED_LANE_TOKENS of the bodies' tokens beyond twice the largest body's, no more than bodies. The
//     first lane reads the costliest part itself, and every lane takes the others from one counter, costliest first
//     (check_deferred_lane): the others are woken for the bodies and read them more slowly. A lane copies the top
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
// A body that does what the merge doesn't take yet sends the unit to source order: a declaration with linkage (the
// file-scope entity's, which later declarations see), a union made transparent, an intrinsic, a report, or a table
// that runs out of room. check_input_deferred then checks the unit with check_input, from nothing, and says so
// (CheckDeferred.unsupported). So does a unit with overloadable functions, whose sets later declarations join.
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
// The bodies' tokens that pay for a lane past the first, beyond twice the largest body's (check_deferred_bodies): on the
// 9700X a second lane paid on units of 13,902 and 14,105 such tokens and lost on ones of 7,553 and 10,185
// (docs/c-deferred-bodies.md, "How many lanes").
#define BUSTER_CHECK_DEFERRED_LANE_TOKENS 12288
// A lane's arenas (CheckDeferredArenas), in this order in one mapping: its reader's records, its scratch and its
// bodies' results of 64 GB each, its copy of the top level's table of 16 GB and the table's index of 4 GB.
#define BUSTER_CHECK_DEFERRED_LANE_ARENAS 5
#define BUSTER_CHECK_DEFERRED_LANE_BYTES ((UINT64_C(3) << 36) + (UINT64_C(1) << 34) + (UINT64_C(1) << 32))

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
    uint32_t asms;
    uint32_t varargs;
    uint32_t constants;
};

// What a body's walk made, in its lane's ids: its type changes, its events' columns, its appended entries, the
// completion positions of the tags it made, the top level's VLAs it changed (its parameters') and the predefined types
// it made.
typedef struct CheckDeferredResult CheckDeferredResult;
struct CheckDeferredResult
{
    TypeChanges changes;
    uint32_t types;
    uint32_t tags;
    uint32_t* tag_completions;
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
// records and scratch, its bodies' results, and its copy of the top level's table and the table's index.
typedef struct CheckDeferredArenas CheckDeferredArenas;
struct CheckDeferredArenas
{
    uint8_t* mapping;
    Arena output;
    Arena scratch;
    Arena results;
    Arena types;
    Arena type_index;
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
    // The largest body's tokens (the first of them), the largest of the others' and the lane that read the largest.
    uint32_t largest_body;
    uint32_t second_body;
    uint32_t largest_index;
    uint32_t largest_lane;
    uint32_t largest_parameters;
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
    // The first lane's word that it has taken every part, which hold_lanes' other lanes wait for; and the other lanes
    // that have taken from the counter, which late_first's first lane waits for.
    uint32_t held;
    uint32_t others_took;
    // The lanes the bodies ran on, as the first records it: `lanes`, or fewer when the bodies' tokens pay for fewer or the
    // bodies are fewer.
    uint32_t active;
    uint64_t top_ns;
    uint64_t environment_ns;
    uint64_t lanes_ns;
    uint64_t merge_ns;
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
            .asms = reader->output->asm_count,
            .varargs = reader->vararg_count,
            .constants = reader->cached_constant_count,
        };
        deferred->body_count += 1;
    }
    deferred->unsupported |= !room;
    deferred->reason = !room ? "more bodies than the mask has" : deferred->reason;
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
// level's table's, which its first reading reserves on the calling thread.
static void check_deferred_arena(Arena* arena, uint64_t capacity)
{
    if (arena->base == 0)
    {
        *arena = arena_reserve(capacity);
    }
    arena_pop_to(arena, 0);
}

// A lane's arenas taken back to empty, reserved first if the state hasn't run the lane yet: whether it has them. They
// are reserved on the calling thread before the lanes start, since reserving memory takes the process's address-space
// lock for writing, on which lanes that reserve at once wait for each other: on the 9700X a lane's copy of the reader,
// which reserved five arenas inside the lane (an mmap and two madvise calls each), took 30 to 70 µs when one lane made
// it and 175 to 295 µs on average when eight did (docs/c-deferred-bodies.md, "How many lanes"). So they are one
// mapping, in base pages throughout: a lane touches its copies of the top level's columns, with room for the largest
// body that it mostly leaves untouched, and its bodies' results, a few megabytes on sqlite3.c, where a huge page would
// be cleared whole the first time any of it is touched.
static bool check_deferred_lane_arenas(CheckDeferredArenas* arenas)
{
    Arena* list[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { &arenas->output, &arenas->scratch, &arenas->results, &arenas->types, &arenas->type_index };
    const uint64_t capacities[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 34, UINT64_C(1) << 32 };
    if (arenas->mapping == 0)
    {
        uint8_t* mapping = mmap(0, BUSTER_CHECK_DEFERRED_LANE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (mapping != MAP_FAILED)
        {
            // The advice only chooses the page size, as arena_reserve's does: the arenas work whether or not the
            // kernel takes it.
            madvise(mapping, BUSTER_CHECK_DEFERRED_LANE_BYTES, MADV_NOHUGEPAGE);
            arenas->mapping = mapping;
            uint64_t offset = 0;
            for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
            {
                *list[i] = (Arena){
                    .base = mapping + offset,
                    .capacity = capacities[i],
                };
                offset += capacities[i];
            }
        }
    }
    for (uint32_t i = 0; i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
    {
        arena_pop_to(list[i], 0);
    }
    return arenas->mapping != 0;
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
    reader->deferred = 0;
    reader->lane = true;
    input->deferred_tokens = 0;
    Arena* arena = &arenas->output;
    // The top level's events, the lane's own after them.
    const uint64_t events = (uint64_t)top->bindings.count + room * 4;
    CBindings* bindings = &reader->bindings;
    bindings->capacity = valid && events < UINT32_MAX ? (uint32_t)events : 0;
    bindings->heads = valid ? check_deferred_copy(arena, 0, 0, (uint64_t)names * C_BINDING_NAMESPACES, sizeof(uint32_t)) : 0;
    bindings->active = bindings->heads != 0 ? check_deferred_copy(arena, 0, 0, events + 1, sizeof(uint32_t)) : 0;
    bindings->key = bindings->active != 0 ? check_deferred_copy(arena, top->bindings.key, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->declaration = bindings->key != 0 ? check_deferred_copy(arena, top->bindings.declaration, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->point = bindings->declaration != 0 ? check_deferred_copy(arena, top->bindings.point, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->end = bindings->point != 0 ? check_deferred_copy(arena, top->bindings.end, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->previous = bindings->end != 0 ? check_deferred_copy(arena, top->bindings.previous, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->depth = bindings->previous != 0 ? check_deferred_copy(arena, top->bindings.depth, top->bindings.count, events + 1, sizeof(uint32_t)) : 0;
    bindings->operation = bindings->depth != 0 ? check_deferred_copy(arena, top->bindings.operation, top->bindings.count, events + 1, sizeof(uint8_t)) : 0;
    bindings->kind = bindings->operation != 0 ? check_deferred_copy(arena, top->bindings.kind, top->bindings.count, events + 1, sizeof(uint8_t)) : 0;
    valid = bindings->kind != 0;
    if (valid)
    {
        memset(bindings->heads, 0, (uint64_t)names * C_BINDING_NAMESPACES * sizeof(uint32_t));
        bindings->base = top->bindings.count;
        bindings->count = top->bindings.count;
        bindings->active_count = 0;
        bindings->valid = true;
    }
    // The per-name and per-tag arrays the walk writes, the scope stacks and the appended tables, the lane's own.
    const uint64_t tags = (uint64_t)lane->types.tags.capacity;
    reader->parameter_seen = valid ? check_deferred_copy(arena, top->parameter_seen, names, names, sizeof(uint32_t)) : 0;
    reader->member_seen = reader->parameter_seen != 0 ? check_deferred_copy(arena, top->member_seen, names, names, sizeof(uint32_t)) : 0;
    reader->linked_declarations = reader->member_seen != 0 ? check_deferred_copy(arena, top->linked_declarations, names, names, sizeof(uint32_t)) : 0;
    reader->linked_types = reader->linked_declarations != 0 ? check_deferred_copy(arena, top->linked_types, names, names, sizeof(TypeId)) : 0;
    reader->builtin_names = reader->linked_types != 0 ? check_deferred_copy(arena, top->builtin_names, names, names, sizeof(uint16_t)) : 0;
    reader->tag_completions = reader->builtin_names != 0 ? check_deferred_copy(arena, top->tag_completions, top->types->tags.count, tags, sizeof(uint32_t)) : 0;
    // Whether each enum has a fixed base: the top level's for its tags, and the lane's own for those its bodies make,
    // which take the indices after the top level's and are set where each is made (check_input_tag_identity).
    reader->enum_fixed = reader->tag_completions != 0 ? check_deferred_copy(arena, top->enum_fixed, top->types->tags.count, tags, sizeof(uint8_t)) : 0;
    valid = reader->enum_fixed != 0;
    if (valid && top->intrinsic_names != 0)
    {
        // The intrinsics' cache, which the lane fills for the names it meets, and their tables, which a body that uses
        // one leaves to source order.
        reader->intrinsic_names = check_deferred_copy(arena, top->intrinsic_names, top->pp->table->count, top->pp->table->count, sizeof(uint32_t));
        input->intrinsic_declarations = check_deferred_copy(arena, top_input->intrinsic_declarations, top_input->intrinsic_declaration_count,
                                                            (uint64_t)top_input->intrinsic_declaration_capacity + 1, sizeof(CheckInputIntrinsicDeclaration));
        input->intrinsic_calls = check_deferred_copy(arena, top_input->intrinsic_calls, top_input->intrinsic_call_count,
                                                     (uint64_t)top_input->intrinsic_call_capacity + 1, sizeof(CheckInputIntrinsicCall));
        valid = reader->intrinsic_names != 0 && input->intrinsic_declarations != 0 && input->intrinsic_calls != 0;
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
    lane->stamps = valid ? check_deferred_copy(arena, 0, 0, names, sizeof(uint32_t)) : 0;
    lane->keys = lane->stamps != 0 ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    lane->points = lane->keys != 0 ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    lane->handles = lane->points != 0 ? check_deferred_copy(arena, 0, 0, keys, sizeof(uint32_t)) : 0;
    valid = lane->handles != 0;
    if (valid)
    {
        memset(lane->stamps, 0, (uint64_t)names * sizeof(uint32_t));
    }
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

// Sets the lane's heads of every name the body (`body`) and its parameters spell to the file scope's at the body's
// opening: the binding state source order's reader has there, for the names the body can look up. Each name once (by
// the lane's stamps), its label head none and its ordinary and tag heads from the history, 16 keys at a time
// (c_environment_lookup_batch), each checked against the restores (check_deferred_visible, which the rare declaration
// that ended takes). The keys stay for check_deferred_heads_clear. Whether the history took every key.
static bool check_deferred_heads(CheckDeferred* deferred, CheckDeferredLane* lane, const CheckDeferredCut* cut, uint32_t body)
{
    CheckInputReader* reader = &lane->reader;
    const Parser* parser = reader->parser;
    CBindings* bindings = &reader->bindings;
    const uint32_t names = bindings->name_capacity;
    const uint32_t stamp = body + 1;
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
                count += 2;
            }
            token = pass == 0 ? token + 1 : reader->parameter_next[token];
        }
    }
    const CEnvironment* environment = &deferred->environment;
    const bool valid = c_environment_lookup_batch(environment, lane->keys, lane->points, count, lane->handles);
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t handle = valid ? lane->handles[i] : 0;
        const bool ended = handle != 0 && environment->bindings->end[handle - 1] != BUSTER_C_BINDING_NONE;
        bindings->heads[lane->keys[i]] = ended ? check_deferred_visible(environment, lane->keys[i], cut->opening, handle) : handle;
    }
    lane->key_count = count;
    lane->history_reads += count / 2;
    return valid;
}

// Takes the heads check_deferred_heads set, and what the walk left in them, back to none.
static void check_deferred_heads_clear(CheckDeferredLane* lane)
{
    CBindings* bindings = &lane->reader.bindings;
    const uint32_t names = bindings->name_capacity;
    for (uint32_t i = 0; i < lane->key_count; i += 2)
    {
        const uint32_t name = lane->keys[i] - C_BINDING_ORDINARY * names;
        bindings->heads[C_BINDING_ORDINARY * names + name] = 0;
        bindings->heads[C_BINDING_TAG * names + name] = 0;
        bindings->heads[C_BINDING_LABEL * names + name] = 0;
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
    reader->cached_constant_count = top->cached_constant_count;
    reader->vararg_count = top->vararg_count;
    input->report_count = 0;
    input->resource = false;
    input->string_count = top_input->string_count;
    input->region_count = top_input->region_count;
    input->vla_count = top_input->vla_count;
    input->asm_count = top_input->asm_count;
    for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        const bool made = top->predefined_positions[i] <= cut->opening;
        reader->predefined_types[i] = made ? top->predefined_types[i] : 0;
        reader->predefined_positions[i] = made ? top->predefined_positions[i] : BUSTER_CHECK_NONE;
    }
    reader->bindings.count = reader->bindings.base;
    reader->bindings.active_count = 0;
    reader->bindings.valid = true;
    const bool history = check_deferred_heads(deferred, lane, cut, body);
    // The table as the top level's was at the opening: its later types hidden, its later definitions withdrawn.
    const TypeViewMark view = type_view_mark(types);
    types->hidden_begin = cut->types;
    types->hidden_end = deferred->top.types.count;
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
    // A completion of a tag the top level made (a definition's parameter-list tag, which its body declares again,
    // #436): everything after the body sees the tag complete in source order, which the merge doesn't carry yet.
    bool completes = false;
    for (uint32_t i = 0; i < changes->count; i += 1)
    {
        const TypeChange* change = changes->items + i;
        completes |= (change->operation == BUSTER_TYPE_CHANGE_LAYOUT || change->operation == BUSTER_TYPE_CHANGE_ENUM) && change->tag < view.tags;
    }
    result->unsupported = !valid || reader->lane_unsupported || input->report_count != 0 || input->resource || !reader->bindings.valid || changes->full ||
                          completes;
    result->reason = !valid || input->resource || changes->full ? "a body ran out of room" : reader->lane_unsupported ? reader->lane_reason :
                     input->report_count != 0 ? "a body reports an error" :
                     !reader->bindings.valid ? "a body's bindings ran out of room" : completes ? "a body completes a tag the top level made" : 0;
    if (!result->unsupported)
    {
        Arena* arena = &lane->arenas->results;
        const uint32_t base = reader->bindings.base;
        const uint32_t events = reader->bindings.count - base;
        bool kept = true;
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
        result->event_count = events;
        result->key = check_deferred_keep(arena, reader->bindings.key + base, events, sizeof(uint32_t), &kept);
        result->declaration = check_deferred_keep(arena, reader->bindings.declaration + base, events, sizeof(uint32_t), &kept);
        result->point = check_deferred_keep(arena, reader->bindings.point + base, events, sizeof(uint32_t), &kept);
        result->end = check_deferred_keep(arena, reader->bindings.end + base, events, sizeof(uint32_t), &kept);
        result->previous = check_deferred_keep(arena, reader->bindings.previous + base, events, sizeof(uint32_t), &kept);
        result->depth = check_deferred_keep(arena, reader->bindings.depth + base, events, sizeof(uint32_t), &kept);
        result->operation = check_deferred_keep(arena, reader->bindings.operation + base, events, sizeof(uint8_t), &kept);
        result->kind = check_deferred_keep(arena, reader->bindings.kind + base, events, sizeof(uint8_t), &kept);
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
    // The costliest part, order[0], is the first lane's own, which it starts at once: the others are woken for the
    // bodies, and a woken lane reads a body at about half the first lane's speed. The counter gives out the others,
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

// Maps an id + 1 (0 for none) through check_deferred_id.
static uint32_t check_deferred_id1(const uint32_t* top, uint32_t top_count, uint32_t base, uint32_t id)
{
    return id != 0 ? check_deferred_id(top, top_count, base, id - 1) + 1 : 0;
}

// Merges every body's result into the top level's reader, in source order: whether everything fit and was replayed.
static bool check_deferred_merge(CheckDeferred* deferred, CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    Arena* scratch = &deferred->memory.arena;
    const uint32_t bodies = deferred->body_count;
    const uint32_t count = parser->tokens.count;
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
    // The events: each body's at its cut, every handle and index into the stream mapped, and a restore's mark past the
    // top level's active declarations at the cut.
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
    for (uint32_t e = 0; valid && e < top_events; e += 1)
    {
        const uint32_t to = event_map[e];
        key[to] = bindings->key[e];
        declaration[to] = bindings->declaration[e];
        point[to] = bindings->point[e];
        end[to] = bindings->end[e] != BUSTER_C_BINDING_NONE ? event_map[bindings->end[e]] : BUSTER_C_BINDING_NONE;
        previous[to] = bindings->previous[e] != 0 ? event_map[bindings->previous[e] - 1] + 1 : 0;
        depth[to] = bindings->depth[e];
        operation[to] = bindings->operation[e];
        kind[to] = bindings->kind[e];
    }
    for (uint32_t b = 0; valid && b < bodies; b += 1)
    {
        const CheckDeferredResult* result = deferred->results + b;
        const uint32_t base = event_bases[b];
        for (uint32_t e = 0; e < result->event_count; e += 1)
        {
            const uint32_t to = base + e;
            key[to] = result->key[e];
            declaration[to] = result->operation[e] == C_BINDING_RESTORE ? result->declaration[e] + deferred->cuts[b].actives : result->declaration[e];
            point[to] = result->point[e];
            end[to] = result->end[e] != BUSTER_C_BINDING_NONE ? check_deferred_id(event_map, top_events, base, result->end[e]) : BUSTER_C_BINDING_NONE;
            previous[to] = check_deferred_id1(event_map, top_events, base, result->previous[e]);
            depth[to] = result->depth[e];
            operation[to] = result->operation[e];
            kind[to] = result->kind[e];
        }
    }
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
                }
            }
        }
    }
    // Every column the walk wrote with a TypeId or an id into those tables, through its owner's part: a body's tokens and
    // its parameters' (which its opening rebinds), and the top level's everywhere else. A body's part maps the top
    // level's ids as the top level's does.
    uint32_t* owners = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    valid = valid && owners != 0;
    if (valid)
    {
        memset(owners, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t b = 0; b < bodies; b += 1)
        {
            const CheckDeferredCut* cut = deferred->cuts + b;
            for (uint32_t token = cut->opening; token <= cut->closing; token += 1)
            {
                owners[token] = b + 1;
            }
            for (uint32_t p = reader->parameters[cut->opening]; p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
            {
                owners[p] = b + 1;
            }
        }
    }
    for (uint32_t token = 0; valid && token <= count; token += 1)
    {
        const bool inside = owners[token] != 0;
        const uint32_t b = inside ? owners[token] - 1 : 0;
        const TypeReplayPart* part = inside ? &parts[1 + b] : &parts[0];
        input->declared_types[token] = check_deferred_type(part, input->declared_types[token]);
        reader->query_types[token] = check_deferred_type(part, reader->query_types[token]);
        reader->return_types[token] = check_deferred_type(part, reader->return_types[token]);
        reader->incomplete_array_types[token] = check_deferred_type(part, reader->incomplete_array_types[token]);
        reader->selection_types[token] = check_deferred_type(part, reader->selection_types[token]);
        reader->tag_types[token] = check_deferred_type(part, reader->tag_types[token]);
        reader->declaration_bases[token] = check_deferred_type(part, reader->declaration_bases[token]);
        const uint32_t string_base = inside ? string_bases[b] : 0;
        const uint32_t region_base = inside ? region_bases[b] : 0;
        const uint32_t vla_base = inside ? vla_bases[b] : 0;
        const uint32_t asm_base = inside ? asm_bases[b] : 0;
        const uint32_t vararg_base = inside ? vararg_bases[b] : 0;
        const uint32_t constant_base = inside ? constant_bases[b] : 0;
        reader->string_ids[token] = reader->string_ids[token] != BUSTER_CHECK_NONE ? check_deferred_id(string_map, top_strings, string_base, reader->string_ids[token]) :
                                    BUSTER_CHECK_NONE;
        input->expression_regions[token] = check_deferred_id1(region_map, top_regions, region_base, input->expression_regions[token]);
        reader->vla_bounds[token] = check_deferred_id1(vla_map, top_vlas, vla_base, reader->vla_bounds[token]);
        reader->vla_ends[token] = check_deferred_id1(vla_map, top_vlas, vla_base, reader->vla_ends[token]);
        input->asm_ids[token] = check_deferred_id1(asm_map, top_asms, asm_base, input->asm_ids[token]);
        reader->vararg_calls[token] = check_deferred_id1(vararg_map, top_varargs, vararg_base, reader->vararg_calls[token]);
        reader->vararg_types[token] = check_deferred_id1(vararg_map, top_varargs, vararg_base, reader->vararg_types[token]);
        reader->constant_present[token] = check_deferred_id1(constant_map, top_constants, constant_base, reader->constant_present[token]);
        CheckInputSpecifierJob* job = reader->specifier_finishes[token];
        if (job != 0)
        {
            job->base = check_deferred_type(part, job->base);
            job->result = check_deferred_type(part, job->result);
        }
    }
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
    for (uint32_t i = 0; valid && i < input->intrinsic_declaration_count; i += 1)
    {
        BuiltinIntrinsicTypeResult* decoded = &input->intrinsic_declarations[i].types;
        decoded->function_type = check_deferred_type(&parts[0], decoded->function_type);
        decoded->result_type = check_deferred_type(&parts[0], decoded->result_type);
        for (uint32_t p = 0; p < decoded->parameter_count && p < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; p += 1)
        {
            decoded->parameters[p] = check_deferred_type(&parts[0], decoded->parameters[p]);
        }
    }
    if (valid)
    {
        (void)top_input;
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
    return valid;
}

static void check_deferred_bodies(CheckDeferred* deferred, CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    deferred->top.changes = 0;
    // A unit whose top level reported, or that has overloadable functions, is source order's.
    bool valid = !deferred->unsupported && input->report_count == 0 && !input->resource && reader->bindings.valid && !deferred->top_changes.full &&
                 input->overload_heads == 0 && deferred->lanes != 0 && deferred->lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES;
    deferred->reason = input->report_count != 0 ? "the top level reports an error" : input->overload_heads != 0 ? "overloadable functions" :
                       !valid ? "the top level ran out of room" : 0;
    deferred->top_reader = reader;
    const uint64_t environment_begin = deferred->timing ? check_deferred_now() : 0;
    deferred->top_ns = deferred->timing ? environment_begin - deferred->top_ns : 0;
    valid = valid && c_environment_build(&deferred->environment, &reader->bindings, &deferred->memory.arena, reader->scratch);
    const uint64_t environment_end = deferred->timing ? check_deferred_now() : 0;
    deferred->environment_ns = environment_end - environment_begin;
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
    // One lane, and one more per BUSTER_CHECK_DEFERRED_LANE_TOKENS of the bodies' tokens beyond twice the largest
    // body's: a lane costs its copy of the reader and its wake-up, which its share of the walks must pay for, and a lane
    // woken for the bodies reads them at about half the first lane's speed, so the lane that takes the largest body may
    // take twice as long as the first would. And no more lanes than bodies, which are at least as many as the parts: a
    // lane past them would take none.
    const uint64_t largest = (uint64_t)deferred->largest_body * 2;
    const uint64_t paid = deferred->every_lane ? UINT64_MAX : 1 + (total > largest ? total - largest : 0) / BUSTER_CHECK_DEFERRED_LANE_TOKENS;
    const uint32_t most = deferred->lanes < deferred->body_count ? deferred->lanes : deferred->body_count;
    const uint32_t active = paid < most ? (uint32_t)paid : most;
    // The lanes' parts, cut here rather than by the lanes, which would wait at lane_parts_by_cost's sync for the last of
    // them to wake: the first lane, this thread, starts its part at once (check_deferred_lane).
    deferred->parts = valid && deferred->body_count != 0 ? parts_by_cost(deferred->costs, deferred->body_count, active, deferred->boundaries, deferred->order) : 0;
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
    deferred->reason = valid && !merged ? "the merge ran out of room" : deferred->reason;
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
            munmap(arenas->mapping, BUSTER_CHECK_DEFERRED_LANE_BYTES);
        }
        *arenas = (CheckDeferredArenas){ 0 };
    }
    arena_release(&memory->top);
    arena_release(&memory->top_index);
    arena_release(&memory->arena);
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
        Arena* list[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { &arenas->output, &arenas->scratch, &arenas->results, &arenas->types, &arenas->type_index };
        for (uint32_t i = 0; arenas->mapping != 0 && i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
        {
            memset(list[i]->base, byte, list[i]->length);
        }
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
    const CheckDeferredMemory memory = deferred->memory;
    *deferred = (CheckDeferred){
        .memory = memory,
        .lanes = lanes,
        .table = types,
        .timing = timing,
        .cpus = cpus,
        .every_lane = every_lane,
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
    if (valid)
    {
        memset(mask, 0, (uint64_t)count + 1);
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t close = parser->tree.parents[token];
            if (parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY && close > token && close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_BODY)
            {
                memset(mask + token, 1, (uint64_t)close - token + 1);
                bodies += 1;
                token = close;
            }
        }
    }
    deferred->cuts = valid ? BUSTER_ARENA_PUSH_ARRAY(&deferred->memory.arena, CheckDeferredCut, (uint64_t)bodies + 1) : 0;
    deferred->body_capacity = bodies;
    valid = valid && deferred->cuts != 0;
    // The top level's table, with the caller's capacities, and its record of changes.
    const TypeCapacity capacity = {
        .types = types->types.capacity - BUSTER_TYPE_BUILTIN_COUNT,
        .parameters = types->parameters.capacity,
        .tags = types->tags.capacity - 1,
        .members = types->members.capacity - 1,
    };
    valid = valid && type_initialize(&deferred->top, &deferred->memory.top, &deferred->memory.top_index, capacity) &&
            type_changes_initialize(&deferred->top_changes, &deferred->memory.top, types->types.capacity + types->tags.capacity * 4, types->parameters.capacity,
                                    types->members.capacity);
    deferred->top.changes = valid ? &deferred->top_changes : 0;
    deferred->reason = !valid ? "the deferred reader's own memory" : 0;
    // check_deferred_bodies ends the top level's span, which starts here.
    deferred->top_ns = timing ? check_deferred_now() : 0;
    bool checked = valid && check_input_run(input, parser, pp, &deferred->top, arena, scratch, true, count, mask, binding_mode, deferred);
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
