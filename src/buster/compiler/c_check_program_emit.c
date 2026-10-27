// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real consumer of CheckProgram: lower, validate, compile, size branches, encode, and publish code atoms and
// their symbolic fixups in the Link shared with static data. check_program assigns source object symbols;
// lowering appends helper and explicit target-runtime symbols before any atom/fixup publication.
// data_capacity reserves the data consumer's atoms/fixups; that consumer defines its already-created symbols.
// The caller performs final link_check after both publications. Output tables/names and byte arenas are distinct
// from scratch; all three roll back on failure. Backend template initialization precedes concurrent workers.
// With `unwind`, every function's unwind table too (#395): its frame as Tier 0 published it (unwind_tier0_frame), at
// its records' ends after branch sizing, written after the last function (unwind_write) as one EH_FRAME atom, a CIE
// and an FDE per function after it, each FDE's initial location a PC32 fixup to its function's symbol; with `verify`,
// each the analyzer's proof from the records too, where the analyzer takes the function (unwind_tier0).

// What an executable's link binds against (the direct path, #157): the C runtime, which bind_begin joins between the
// program's symbols and its atoms, its arrays from `arena` and its atoms' bytes copied into the emission's byte arena.
// check_program_emit makes room for it in the link and calls bind_begin; its caller adds the static data and calls
// bind_end. An object's emission has none.
typedef struct CheckProgramBinding CheckProgramBinding;
struct CheckProgramBinding
{
    const Runtime* runtime;
    Bind* bind;
    BindPath path;
    Arena* arena;
    // The static data the caller publishes after the code (check_data_prepare's objects): binding takes their symbols,
    // as the bodies' and the helpers', for the program's definitions before their atoms join.
    const CheckDataObject* data_objects;
    uint32_t data_count;
    // When bind_begin fails about a name (Bind.name), its bytes, since a failed emission rolls its link back.
    char failed_name[256];
};

typedef struct CheckProgramCode CheckProgramCode;
struct CheckProgramCode
{
    Link link;
    CheckProgramLowered lowered;
    AtomId* function_atoms;
    CheckProgramStatus status;
    Tier0Error backend_error;
    uint32_t token;
    uint32_t backend_node;
    // CheckProgramLowered.reason, or the attribute or construct the emission refused.
    const char* reason;
    uint64_t records;
    uint64_t deferred;
    uint64_t code_bytes;
};

// The local symbols whose names the compiler makes are named as clang names them, so that tools and #129's comparison
// with clang's objects (c_lowering_symbols.c) read them alike: a block-scope static is "function.name", its function's
// symbol name (its asm label, if it has one) and its own (an asm label of the static's own replaces both), and a
// file-scope compound literal is ".compoundliteral". A made name that a symbol has already (one the source gives, an
// identifier or an asm label, or one made before) gets ".N", N from one counter for the unit, as LLVM makes a name
// unique in its module. `taken` is a table of mask + 1 NameIds, 0 for an empty slot, with room for every symbol's name
// and at least as many empty slots.
typedef struct CheckProgramNames CheckProgramNames;
struct CheckProgramNames
{
    NameId* taken;
    uint32_t mask;
    uint32_t unique;
};

// Whether a name of `names` is the `length` bytes at `bytes`, of hash `hash`: its slot in `slot` if so, and the empty
// slot it would take if not.
static bool check_program_emit_taken(const Link* link, const CheckProgramNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash, uint32_t* slot)
{
    uint32_t probe = (uint32_t)hash & names->mask;
    bool taken = false;
    while (names->taken[probe] != 0 && !taken)
    {
        const NameId other = names->taken[probe];
        taken = link->names.hash[other] == hash && link->names.length[other] == length && memcmp(link->names.bytes[other], bytes, length) == 0;
        probe = taken ? probe : (probe + 1) & names->mask;
    }
    *slot = probe;
    return taken;
}

// Whether the compiler makes the name of `declaration`'s symbol (`renamed`: its asm label, if it has one): a file-scope
// compound literal's, and a block-scope static's without a label of its own.
static bool check_program_emit_makes_name(const CheckInput* input, uint32_t declaration, String8 renamed)
{
    return input->compound_objects[declaration] ||
           (renamed.length == 0 && input->declaration_bodies[declaration] != BUSTER_CHECK_NONE && input->linkage[declaration] == 0);
}

// Adds `spelling`, the name the source gives a symbol, to the link's names and to the names a made one must not be: its
// NameId, or 0 when the link has no room.
static NameId check_program_emit_given_name(Link* link, CheckProgramNames* names, const uint8_t* spelling, uint32_t length)
{
    const uint64_t hash = link_hash(spelling, length);
    uint32_t slot = 0;
    const bool taken = check_program_emit_taken(link, names, spelling, length, hash, &slot);
    const NameId result = link_add_name_hashed(link, spelling, length, hash);
    if (!taken && result != 0)
    {
        names->taken[slot] = result;
    }
    return result;
}

// Adds `base`, or the first of its ".N" spellings no name of `names` has, to the link's names: its NameId, or 0 when the
// link or scratch has no room.
static NameId check_program_emit_made_name(Link* link, CheckProgramNames* names, const uint8_t* base, uint32_t base_length, Arena* scratch)
{
    // ".N" takes at most 11 bytes past the base, and snprintf a 12th for its NUL.
    uint8_t* candidate = base_length <= UINT32_MAX - 12 ? arena_push(scratch, (uint64_t)base_length + 12, 1) : 0;
    NameId result = 0;
    if (candidate != 0)
    {
        memcpy(candidate, base, base_length);
        uint32_t length = base_length;
        uint64_t hash = link_hash(candidate, length);
        uint32_t slot = 0;
        while (check_program_emit_taken(link, names, candidate, length, hash, &slot))
        {
            names->unique += 1;
            length = base_length + (uint32_t)snprintf((char*)candidate + base_length, 12, ".%u", names->unique);
            hash = link_hash(candidate, length);
        }
        result = link_add_name_hashed(link, candidate, length, hash);
        names->taken[slot] = result;
    }
    return result;
}

// The raw rows' room (CheckProgramRows): each body's entries, its prologue's and its epilogue's. A program whose
// selection kept no function has none, an empty set of rows (a unit of no functions to debug.c), not a failure. Only -g
// reaches it, out of line so that the plain emission carries none of it.
static __attribute__((noinline)) bool check_program_emit_reserve_rows(CheckProgramRows* rows, uint64_t entries, uint32_t count, Arena* output)
{
    const uint64_t capacity = entries + 2 * (uint64_t)count;
    const bool fits = capacity <= UINT32_MAX - BUSTER_CHECK_DEBUG_SLACK;
    *rows = (CheckProgramRows){ .capacity = fits ? (uint32_t)capacity : 0 };
    rows->offsets = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)rows->capacity + BUSTER_CHECK_DEBUG_SLACK) : 0;
    rows->tokens = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)rows->capacity + BUSTER_CHECK_DEBUG_SLACK) : 0;
    rows->first = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->counts = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->sizes = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->bodies = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    return rows->offsets != 0 && rows->tokens != 0 && rows->first != 0 && rows->counts != 0 && rows->sizes != 0 && rows->bodies != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The functions on lanes (#151)
//
// check_program_emit_lanes runs each function's lowering, Tier 0, branch sizing and encoding as a lane item, cut into
// parts of consecutive bodies by their tokens (lane_parts_by_cost) and taken costliest first (lane_take), each lane with
// a writer and arenas of its own; then one lane, the caller's, adds the functions' atoms, symbols and fixups to the link
// in source order, so that the link, and the object or executable made from it, don't depend on the lane count. A part
// takes the steps source order's emission takes for the whole program, in the same order: its bodies' lowering
// (check_lower_part), then each body's Tier 0 code and, while the part has no failure, its encoding. The failure
// reported is source order's: the first step's that fails, its first body's (check_lower_finish orders the lowering's,
// and the merge Tier 0's, then the link's and its bindings', then the encodings'). A part that fails stops, but past a
// failure after Tier 0 it goes on with Tier 0 alone, whose failures come first. With one lane its bodies are one part,
// and the emission takes source order's steps in source order's order.

// The most lanes an emission runs.
#define BUSTER_CHECK_EMIT_MAX_LANES 64
// The bodies' tokens that pay for a lane past the first, beyond twice the largest body's (check_program_emit_lanes): a
// lane costs its thread's start, its writer's columns and its arenas' first pages, which its share of the bodies must pay
// for, and the lane that takes the largest body takes at least as long as it.
#define BUSTER_CHECK_EMIT_LANE_TOKENS 4096
// A lane's arenas, as CheckProgramLanes.squeeze_arenas names them (the tests').
#define BUSTER_CHECK_EMIT_SQUEEZE_OUTPUT 1u
#define BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH 2u
#define BUSTER_CHECK_EMIT_SQUEEZE_BYTES 4u

// Why an emission on lanes ran its bodies again on the calling thread alone (CheckProgramLanes.again): its lanes didn't
// start (lane_run, which counts a lane it couldn't pin as one), or a part ran out of room in its lowering (or its lane
// had none for its writer), in a body's Tier 0 code or in a body's code after Tier 0. One lane's arenas, the caller's,
// then decide whether the unit fits.
typedef enum CheckProgramAgain
{
    CHECK_PROGRAM_AGAIN_NONE,
    CHECK_PROGRAM_AGAIN_START,
    CHECK_PROGRAM_AGAIN_LOWERING,
    CHECK_PROGRAM_AGAIN_TIER0,
    CHECK_PROGRAM_AGAIN_CODE,
} CheckProgramAgain;

// A lane's arenas past the first lane's, which are the caller's: its functions' lowered columns (`output`), its writer's
// columns, its functions' Tier 0 code and what their fixups and rows need until the merge (`scratch`), and its
// functions' bytes (`bytes`).
typedef struct CheckProgramLaneArenas CheckProgramLaneArenas;
struct CheckProgramLaneArenas
{
    Arena output;
    Arena scratch;
    Arena bytes;
};

// How check_program_emit_lanes runs a unit's functions on lanes, and what it ran. The caller keeps it until it is done
// with the emission's link and lowering: the atoms' bytes and the functions' columns of the lanes past the first are in
// their arenas here (check_program_lanes_release), which the next emission with it reuses.
typedef struct CheckProgramLanes CheckProgramLanes;
struct CheckProgramLanes
{
    // The most lanes to run (0 or 1 for one), and whether to run all of them, as long as there are bodies for them,
    // whatever the bodies' tokens (the tests'); the lanes' CPUs for lane_run, or 0.
    uint32_t lanes;
    bool every_lane;
    const uint32_t* cpus;
    // A test's lane that sleeps `slow_microseconds` after each part it takes, its index plus one (0 for none), so that the
    // other lanes take the parts it would have and finish theirs first.
    uint32_t slow_lane;
    uint32_t slow_microseconds;
    // A test's lane short of room: the lane that takes the part holding body `squeeze_body` (its index plus one, 0 for
    // none) has only `squeeze_room` bytes to spare in each of its arenas that `squeeze_arenas` names
    // (BUSTER_CHECK_EMIT_SQUEEZE_*), as if they were that full, while it makes its writer, if that is its first part, and
    // works on that part. What fails is a resource's, which runs the emission again on one lane, without the squeeze.
    uint32_t squeeze_body;
    uint32_t squeeze_arenas;
    uint64_t squeeze_room;
    // What the last emission ran: its lanes and parts, the lane that took the squeezed part (its index plus one, 0 for
    // none), and whether it ran them again on one lane and why; with `timing`, how long its steps took: from its start to
    // the lanes' (the lowering's preparation and the access column), the lanes' run (with their threads' start), and from
    // the lanes' end to its own (the merge), in nanoseconds (the benchmark's).
    uint32_t active;
    uint32_t parts;
    uint32_t squeezed;
    CheckProgramAgain again;
    bool timing;
    uint64_t prepare_ns;
    uint64_t lanes_ns;
    uint64_t merge_ns;
    CheckProgramLaneArenas arenas[BUSTER_CHECK_EMIT_MAX_LANES];
};

// CLOCK_MONOTONIC, for CheckProgramLanes.timing.
static uint64_t check_program_lanes_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Releases the lanes' arenas: after the caller is done with the last emission's link.
static void check_program_lanes_release(CheckProgramLanes* lanes)
{
    for (uint32_t lane = 0; lanes != NULL && lane < BUSTER_CHECK_EMIT_MAX_LANES; lane += 1)
    {
        arena_release(&lanes->arenas[lane].output);
        arena_release(&lanes->arenas[lane].scratch);
        arena_release(&lanes->arenas[lane].bytes);
    }
}

// A body's code, as its lane made it (check_program_emit_code): its bytes, encoded after branch sizing; each marked
// instruction's end (x86_encode's fixup ends) and each label's offset (or UINT32_MAX for a record past the code), for the
// merge's link; with -g its raw rows, with room for its entries, its prologue's, its epilogue's and the slack; and its
// failure after Tier 0, as source order's emission reports it (CHECK_PROGRAM_OK for none): at its declaration but for a
// resource's.
typedef struct CheckEmitBody CheckEmitBody;
struct CheckEmitBody
{
    uint8_t* bytes;
    uint64_t length;
    uint32_t* fixup_ends;
    uint32_t* label_offsets;
    uint32_t* row_offsets;
    uint32_t* row_tokens;
    uint32_t row_count;
    uint32_t body_offset;
    CheckProgramStatus status;
    uint32_t token;
    const char* reason;
};

// A part's steps (CheckLowerPart, the lowering's), and its first body whose Tier 0 code failed and its first whose code
// failed after Tier 0, its end for none.
typedef struct CheckEmitPart CheckEmitPart;
struct CheckEmitPart
{
    CheckLowerPart lower;
    uint32_t tier0_failed;
    uint32_t code_failed;
};

// A lane's writer of its parts' lowering (check_lower_writer_begin's), made when it takes its first part, and its arenas:
// the caller's for lane 0, CheckProgramLanes.arenas' for the others.
typedef struct CheckEmitLane CheckEmitLane;
struct CheckEmitLane
{
    CheckLowerWriter writer;
    CheckLowerAbi abi;
    CheckProgramLowered status;
    Arena* output;
    Arena* scratch;
    Arena* bytes;
    bool prepared;
    bool ready;
};

// What every lane of an emission reads (`program` to `frames`, and the parts' cut), and what each fills: the bodies'
// Tier0Functions (CheckProgramLowered.functions), their codes and their frames, each body's by the lane that takes it,
// and each part's result.
typedef struct CheckEmitRun CheckEmitRun;
struct CheckEmitRun
{
    const CheckProgram* program;
    const Parser* parser;
    const CheckLowerShared* shared;
    CheckProgramLowered* lowered;
    CheckProgramLanes* lanes;
    const uint8_t* access;
    uint32_t access_count;
    uint32_t body_count;
    bool scalar;
    bool verify;
    bool unwind;
    bool sources;
    Arena* output;
    Arena* scratch;
    Arena* bytes;
    Tier0Code* codes;
    CheckEmitBody* bodies;
    UnwindFunction* frames;
    uint64_t* costs;
    uint64_t* boundaries;
    uint32_t* orders[BUSTER_CHECK_EMIT_MAX_LANES];
    CheckEmitPart* parts;
    uint32_t part_count;
    CheckEmitLane* lane_states;
};

// Body `b`'s raw source rows (c_check_debug.c), from where each entry's code starts now that the branches are sized: the
// record count's offset is the end. Only -g reaches it, out of line so that the plain emission carries none of it.
static __attribute__((noinline)) void check_program_emit_body_rows(CheckEmitBody* body, const CheckProgram* program, const Parser* parser,
                                                                   const uint32_t* tokens, const Tier0Code* code, uint32_t b, uint32_t* offsets, uint32_t length,
                                                                   bool scalar)
{
    offsets[code->count] = length;
    const uint32_t declaration = program->bodies[b].declaration;
    const uint32_t closing = parser->tree.parents[program->bodies[b].opening];
    body->row_count = scalar ? check_debug_rows_scalar(code->entry_records, tokens, code->entry_count, offsets, length, declaration, closing,
                                                       body->row_offsets, body->row_tokens) :
                               check_debug_rows(code->entry_records, tokens, code->entry_count, offsets, length, declaration, closing, body->row_offsets,
                                                body->row_tokens);
    body->body_offset = offsets[code->entry_records[0]];
}

// Body `b`'s code after Tier 0, on its lane (`lane`): branch sizing, encoding into the lane's bytes, its unwind frame, its
// labels' offsets and with -g its raw rows, and what its fixups need. Returns false for a failure, which it records in
// the body as source order's emission reports it: no room; a record x86_validate rejects (with `verify`); or a sizing
// or an encoding Tier 0's records shouldn't have, or a frame other than its unwind table's, at its declaration.
static bool check_program_emit_code(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b)
{
    const CheckProgram* program = run->program;
    const Tier0Code* code = run->codes + b;
    CheckEmitBody* body = run->bodies + b;
    Arena* scratch = lane->scratch;
    Arena* bytes = lane->bytes;
    *body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    // What the merge reads stays on the lane's scratch; the offsets don't.
    body->fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
    body->label_offsets = code->label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, code->label_count) : 0;
    const uint64_t rows = run->sources ? (uint64_t)code->entry_count + 2 + BUSTER_CHECK_DEBUG_SLACK : 0;
    body->row_offsets = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    body->row_tokens = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    const uint64_t work_mark = scratch->length;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
    const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t before = bytes->length;
    uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
    bool valid = offsets != 0 && body->fixup_ends != 0 && encoded != 0 && (code->label_count == 0 || body->label_offsets != 0) &&
                 (!run->sources || (body->row_offsets != 0 && body->row_tokens != 0));
    body->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
    if (valid && run->verify && x86_validate(code->records, code->count) != code->count)
    {
        body->status = CHECK_PROGRAM_BACKEND;
        body->reason = "internal compiler error: a record of Tier 0's fails x86_validate";
        body->token = program->bodies[b].declaration;
        valid = false;
    }
    if (valid)
    {
        const BranchSizingCheck shape = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
        const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                               run->scalar ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                             x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
        valid = length >= 0;
        if (valid && run->sources)
        {
            check_program_emit_body_rows(body, program, run->parser, run->lowered->entry_tokens[b], code, b, offsets, (uint32_t)length, run->scalar);
        }
        if (valid && run->unwind)
        {
            // A prologue or epilogue that isn't the frame Tier 0 published is the compiler's own failure; and with --verify,
            // so is a frame the analyzer proves otherwise from the records, where it takes the function (a fixed frame's,
            // and not a computed goto's).
            valid = length <= INT32_MAX && unwind_tier0_frame(code, offsets, (uint32_t)length, &run->frames[b]) == UNWIND_OK;
            UnwindFunction proven;
            const UnwindError analyzed = !valid || !run->verify ? UNWIND_ERROR_UNSUPPORTED :
                                         run->scalar ? unwind_tier0_scalar(code, offsets, (uint32_t)length, &proven) :
                                                       unwind_tier0(code, offsets, (uint32_t)length, &proven);
            valid = valid && (analyzed != UNWIND_OK || memcmp(&proven, &run->frames[b], sizeof(proven)) == 0);
            body->reason = valid ? body->reason : "internal compiler error: a function's prologue or epilogue isn't the frame its unwind table describes";
        }
        if (valid)
        {
            const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length;
            arena_pop_to(bytes, end > before ? end : before);
            body->bytes = encoded;
            body->length = (uint64_t)length;
            for (uint32_t k = 0; k < code->label_count; k += 1)
            {
                body->label_offsets[k] = code->label_records[k] < code->count ? offsets[code->label_records[k]] : UINT32_MAX;
            }
        }
        else
        {
            body->status = CHECK_PROGRAM_BACKEND;
            body->token = program->bodies[b].declaration;
        }
    }
    if (!valid)
    {
        arena_pop_to(bytes, before);
    }
    arena_pop_to(scratch, work_mark);
    return valid;
}

// A part of the bodies on lane `lane`: their lowering (check_lower_part), then each one's Tier 0 code, the lane's
// function access column given, and its code after Tier 0 (check_program_emit_code) until one of them fails.
static void check_program_emit_part(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part)
{
    if (run->sources)
    {
        check_lower_part_sources(&lane->writer, run->shared, &part->lower, lane->output, lane->scratch);
    }
    else
    {
        check_lower_part(&lane->writer, run->shared, &part->lower, lane->output, lane->scratch);
    }
    const uint32_t end = part->lower.end;
    part->tier0_failed = end;
    part->code_failed = end;
    for (uint32_t b = part->lower.first; part->lower.stage == CHECK_LOWER_STAGE_NONE && part->tier0_failed == end && b < end; b += 1)
    {
        Tier0Function* function = &run->lowered->functions[b];
        function->symbol_access = run->access;
        function->symbol_count = run->access_count;
        // The entries' first records only for source rows.
        function->entry_records = run->sources;
        run->codes[b] = run->scalar ? tier0_compile_scalar(function, lane->scratch) : tier0_compile(function, lane->scratch);
        if (run->codes[b].error != TIER0_OK)
        {
            part->tier0_failed = b;
        }
        else if (part->code_failed == end && !check_program_emit_code(run, lane, b))
        {
            part->code_failed = b;
        }
    }
}

// Lane `index`'s arenas, for its first part: the caller's for lane 0, and for another lane its own, reserved the first
// time and otherwise put back (the emission before this one's are done with). Returns false if one can't be reserved.
static bool check_program_emit_lane_arenas(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    bool valid = true;
    if (index == 0)
    {
        lane->output = run->output;
        lane->scratch = run->scratch;
        lane->bytes = run->bytes;
    }
    else
    {
        CheckProgramLaneArenas* arenas = run->lanes->arenas + index;
        Arena* each[3] = { &arenas->output, &arenas->scratch, &arenas->bytes };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (each[i]->base == 0)
            {
                // The writer's columns (the scratch) have room for any of the program's bodies each, far apart, of which a
                // lane touches a little at each column's start: in base pages, as only those are cleared, where a huge
                // page each, cleared whole, made 8 lanes' kernel time 40 times one lane's on the 9700X. The functions'
                // columns and bytes are packed, and take huge pages past the first 64 KB (arena_reserve).
                *each[i] = i == 1 ? arena_reserve_small(UINT64_C(1) << 36, UINT64_C(1) << 36) : arena_reserve(UINT64_C(1) << 36);
            }
            arena_pop_to(each[i], 0);
            valid = valid && each[i]->base != 0;
        }
        lane->output = &arenas->output;
        lane->scratch = &arenas->scratch;
        lane->bytes = &arenas->bytes;
    }
    return valid;
}

// Lane `index`'s arenas and writer, for its first part.
static bool check_program_emit_lane_prepare(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    return check_program_emit_lane_arenas(run, lane, index) &&
           check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->scratch);
}

// A part's failure before it lowered anything: its lane had no room for its writer.
static void check_program_emit_part_refused(CheckEmitPart* part)
{
    part->lower.stage = CHECK_LOWER_STAGE_BODY;
    part->lower.status = CHECK_PROGRAM_RESOURCE;
    part->lower.token = BUSTER_CHECK_NONE;
    part->lower.backend_node = 0;
    part->lower.reason = NULL;
    part->lower.final_token = BUSTER_CHECK_NONE;
    part->lower.entries = 0;
    part->lower.label_symbol_count = 0;
    part->lower.runtime_count = 0;
    part->tier0_failed = part->lower.end;
    part->code_failed = part->lower.end;
}

// The part `part` on lane `index`, which holds the test's squeezed body (CheckProgramLanes.squeeze_body): each of the
// lane's arenas that squeeze_arenas names has only squeeze_room bytes to spare while the lane makes its writer, if this
// is its first part, and works on the part; then its room comes back. Out of line, so that the lanes' loop carries none
// of it.
static __attribute__((noinline)) void check_program_emit_squeezed(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part, uint32_t index)
{
    CheckProgramLanes* lanes = run->lanes;
    const bool arenas = lane->prepared ? lane->ready : check_program_emit_lane_arenas(run, lane, index);
    Arena* each[3] = { lane->output, lane->scratch, lane->bytes };
    uint64_t capacities[3] = { 0, 0, 0 };
    for (uint32_t i = 0; arenas && i < 3; i += 1)
    {
        capacities[i] = each[i]->capacity;
        const bool named = (lanes->squeeze_arenas & (UINT32_C(1) << i)) != 0;
        each[i]->capacity = named && lanes->squeeze_room < capacities[i] - each[i]->length ? each[i]->length + lanes->squeeze_room : capacities[i];
    }
    lane->ready = lane->prepared ? lane->ready :
                                   arenas && check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->scratch);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
    for (uint32_t i = 0; arenas && i < 3; i += 1)
    {
        each[i]->capacity = capacities[i];
    }
    lanes->squeezed = index + 1;
}

// Every lane's entry: the parts by cost (CheckEmitRun.costs), taken costliest first. A lane prepares its writer when it
// takes its first part, so that one that takes none spends nothing on it.
static void check_program_emit_lane(void* argument)
{
    CheckEmitRun* run = argument;
    const uint32_t index = lane_index();
    CheckEmitLane* lane = run->lane_states + index;
    const uint64_t parts = lane_parts_by_cost(run->costs, run->body_count, run->boundaries, run->orders[index]);
    for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
    {
        const uint32_t part = run->orders[index][taken];
        CheckEmitPart* result = run->parts + part;
        result->lower.first = (uint32_t)run->boundaries[part];
        result->lower.end = (uint32_t)run->boundaries[part + 1];
        const uint32_t squeeze = run->lanes->squeeze_body;
        if (squeeze != 0 && squeeze - 1 >= result->lower.first && squeeze - 1 < result->lower.end)
        {
            check_program_emit_squeezed(run, lane, result, index);
        }
        else
        {
            lane->ready = lane->prepared ? lane->ready : check_program_emit_lane_prepare(run, lane, index);
            lane->prepared = true;
            if (lane->ready)
            {
                check_program_emit_part(run, lane, result);
            }
            else
            {
                check_program_emit_part_refused(result);
            }
        }
        if (run->lanes->slow_lane == index + 1)
        {
            const struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)run->lanes->slow_microseconds * 1000 };
            nanosleep(&pause, NULL);
        }
    }
    if (index == 0)
    {
        run->part_count = (uint32_t)parts;
    }
}

// The bodies as one part on the calling thread, without the lanes' calls: one lane's emission, which a caller running in
// a group of lanes of its own may make.
static void check_program_emit_alone(CheckEmitRun* run)
{
    CheckEmitLane* lane = run->lane_states;
    CheckEmitPart* part = run->parts;
    part->lower.first = 0;
    part->lower.end = run->body_count;
    run->part_count = 1;
    lane->ready = check_program_emit_lane_prepare(run, lane, 0);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
}

// The objects the unit defines, a byte per object in `defined` (1 where it does), and how Tier 0 reaches each of the
// program's symbols, `access` 1 to symbol_count (Tier0Function.symbol_access): a symbol this unit doesn't define may be a
// shared library's, which the direct path, without copy relocations, reaches only through its GOT slot (an executable's
// writer relaxes the load where the symbol is in the image), and a thread-local is the image's by local-exec, or
// another's by initial-exec. The unit defines its bodies' functions, the objects it defines or tentatively defines (as
// check_data_prepare selects them), its literals and whatever has no external linkage. The emission's column
// (check_program_emit_lanes) and check_program_access's share it.
static void check_program_object_access(const CheckProgram* program, uint8_t* access, uint8_t* defined)
{
    memset(defined, 0, (uint64_t)program->object_count + 1);
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        defined[program->bodies[b].object] = 1;
    }
    for (uint32_t token = 0; token < program->input->token_count; token += 1)
    {
        const uint32_t object = program->input->objects[token];
        if (program->input->declarations[token] == token && object < program->object_count && program->input->declaration_kinds[token] == C_BINDING_OBJECT &&
            (program->input->defined[token] || program->input->tentative[token]))
        {
            defined[object] = 1;
        }
    }
    for (uint32_t s = 0; s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        const bool literal = declaration == BUSTER_CHECK_NONE || program->input->compound_objects[declaration];
        const bool own = literal || defined[object] || program->input->linkage[declaration] != 2;
        const bool thread_local = !literal && program->input->thread_local[declaration];
        access[s + 1] = thread_local ? (own ? BUSTER_TIER0_ACCESS_LOCAL_EXEC : BUSTER_TIER0_ACCESS_INITIAL_EXEC) : own ? BUSTER_TIER0_ACCESS_DIRECT : BUSTER_TIER0_ACCESS_GOT;
    }
}

// How Tier 0 reaches each symbol of a lowered program, as check_lower_finish numbered them (Tier0Function.symbol_access):
// one column, from `output`, for every body that `lowered` holds of `program`, and `*defined_objects`, from `scratch`
// (check_program_object_access). The helpers are the unit's, and the C runtime's errno function isn't; libgcc's
// functions are linked into the image. Returns whether there was room. benchmark-backend-source (tier0_source_benchmark.c)
// compiles the lowered functions again with it: the emission's own column, made before its lanes, has the runtime
// functions' provisional symbols (check_lower_runtime), and goes with its scratch.
static bool check_program_access(CheckProgramLowered* lowered, const CheckProgram* program, Arena* output, Arena* scratch, uint8_t** defined_objects)
{
    const uint32_t count = program->body_count;
    const uint32_t access_count = program->symbol_count + lowered->helpers.count + lowered->runtime_symbol_count + 1;
    uint8_t* access = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, access_count);
    uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1);
    const bool valid = access != 0 && defined != 0;
    *defined_objects = defined;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined);
        for (uint32_t r = 0; r < lowered->runtime_symbol_count; r += 1)
        {
            const uint32_t symbol = program->symbol_count + lowered->helpers.count + 1 + r;
            access[symbol] = lowered->runtime_order[r] == CHECK_LOWER_RUNTIME_ERRNO ? BUSTER_TIER0_ACCESS_GOT : BUSTER_TIER0_ACCESS_DIRECT;
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            lowered->functions[b].symbol_access = access;
            lowered->functions[b].symbol_count = access_count;
        }
    }
    return valid;
}

// With `verify`, every function's records must pass x86_validate before they are sized and encoded (the tests', and
// #145's c-testsuite harness's, check of Tier 0's output), or the emission fails with CHECK_PROGRAM_BACKEND. With
// `rows`, each source body's raw source rows too (c_check_debug.c), from `output`. With `lanes`, on up to lanes->lanes
// lanes (above): one, and one more per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens beyond twice the largest
// body's, but with lanes->every_lane as many as there are parts for; without, on the calling thread alone.
static bool check_program_emit_lanes(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                                     const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                                     CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify, CheckProgramLanes* lanes)
{
    *result = (CheckProgramCode){ .token = BUSTER_CHECK_NONE };
    const bool timing = lanes != NULL && lanes->timing;
    const uint64_t started = timing ? check_program_lanes_now() : 0;
    const uint64_t mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t temporary = scratch->length;
    bool valid = output != bytes && output != scratch && bytes != scratch && program->status == CHECK_PROGRAM_OK;
    result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && !tier0_initialized)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    if (valid && unwind && !unwind_ready)
    {
        valid = unwind_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    CheckLowerShared shared;
    const bool lowering = valid;
    if (valid)
    {
        valid = check_lower_prepare(&shared, &result->lowered, program, scalar, rows != NULL, output, scratch);
    }
    const uint32_t count = program->body_count;
    // How Tier 0 reaches each symbol (Tier0Function.symbol_access), one column for every body: the program's symbols' as
    // check_program_object_access gives them. The helpers are the unit's, and the C runtime's errno function isn't;
    // libgcc's functions are linked into the image. The runtime's functions have their provisional symbols while the
    // bodies compile (check_lower_runtime), each its own.
    const uint32_t access_count = valid ? shared.provisional_runtimes + CHECK_LOWER_RUNTIMES : 0;
    uint8_t* access = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, access_count) : 0;
    uint8_t* defined = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    valid = valid && access != 0 && defined != 0;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined);
        access[shared.provisional_runtimes + CHECK_LOWER_RUNTIME_ERRNO] = BUSTER_TIER0_ACCESS_GOT;
    }
    // The lanes' run: the bodies' codes and code, their frames, and the parts' cut by the bodies' tokens.
    const uint64_t frames = unwind ? (uint64_t)count + (valid ? result->lowered.helpers.count : 0) : 0;
    CheckEmitRun run = {
        .program = program,
        .parser = parser,
        .shared = &shared,
        .lowered = &result->lowered,
        .lanes = lanes,
        .access = access,
        .access_count = access_count,
        .body_count = count,
        .scalar = scalar,
        .verify = verify,
        .unwind = unwind,
        .sources = rows != NULL,
        .output = output,
        .scratch = scratch,
        .bytes = bytes,
    };
    run.codes = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Code, count) : 0;
    run.bodies = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitBody, count) : 0;
    run.frames = valid && frames != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, UnwindFunction, frames) : 0;
    run.costs = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    valid = valid && (count == 0 || (run.codes != 0 && run.bodies != 0)) && (frames == 0 || run.frames != 0) && run.costs != 0;
    uint64_t total = 0;
    uint64_t largest = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const uint32_t opening = program->bodies[b].opening;
        const uint64_t tokens = (uint64_t)parser->tree.parents[opening] - opening + 1;
        total += tokens;
        largest = tokens > largest ? tokens : largest;
        run.costs[b] = total;
    }
    // One lane, and one more per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens beyond twice the largest body's; no
    // more than the bodies, which are at least as many as the parts.
    const uint32_t most = lanes != NULL && lanes->lanes > 1 ? (lanes->lanes < BUSTER_CHECK_EMIT_MAX_LANES ? lanes->lanes : BUSTER_CHECK_EMIT_MAX_LANES) : 1;
    const uint64_t paid = lanes != NULL && lanes->every_lane ? UINT64_MAX : 1 + (total > 2 * largest ? total - 2 * largest : 0) / BUSTER_CHECK_EMIT_LANE_TOKENS;
    const uint32_t bodies = count != 0 ? count : 1;
    uint32_t active = most < bodies ? most : bodies;
    active = paid < active ? (uint32_t)paid : active;
    const uint64_t part_room = active > 1 ? (uint64_t)BUSTER_LANE_PARTS_PER_LANE * active : 1;
    run.parts = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitPart, part_room) : 0;
    run.lane_states = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitLane, active) : 0;
    run.boundaries = valid && active > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, part_room + 1) : 0;
    for (uint32_t lane = 0; valid && active > 1 && lane < active; lane += 1)
    {
        run.orders[lane] = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, part_room + 1);
        valid = run.orders[lane] != 0;
    }
    valid = valid && run.parts != 0 && run.lane_states != 0 && (active == 1 || run.boundaries != 0);
    CheckProgramAgain again = CHECK_PROGRAM_AGAIN_NONE;
    if (lanes != NULL)
    {
        lanes->squeezed = 0;
    }
    const uint64_t lanes_begin = timing ? check_program_lanes_now() : 0;
    if (valid && count != 0)
    {
        memset(run.lane_states, 0, (uint64_t)active * sizeof(CheckEmitLane));
        // The lanes' run, or the calling thread's alone; lanes that couldn't start, or a part that ran out of room, go
        // again on the calling thread alone, from the arenas as they were (each body sets what it reads of the shared
        // tables before it reads it, and puts local_map's entries back).
        const uint64_t output_before = output->length;
        const uint64_t bytes_before = bytes->length;
        const uint64_t scratch_before = scratch->length;
        if (active > 1)
        {
            again = lane_run(active, check_program_emit_lane, &run, lanes->cpus) ? CHECK_PROGRAM_AGAIN_NONE : CHECK_PROGRAM_AGAIN_START;
            for (uint32_t p = 0; again == CHECK_PROGRAM_AGAIN_NONE && p < run.part_count; p += 1)
            {
                const CheckEmitPart* part = run.parts + p;
                const bool tier0 = part->tier0_failed != part->lower.end && run.codes[part->tier0_failed].error == TIER0_ERROR_MEMORY;
                const bool code = part->code_failed != part->lower.end && run.bodies[part->code_failed].status == CHECK_PROGRAM_RESOURCE;
                again = part->lower.status == CHECK_PROGRAM_RESOURCE ? CHECK_PROGRAM_AGAIN_LOWERING :
                        tier0 ? CHECK_PROGRAM_AGAIN_TIER0 :
                        code ? CHECK_PROGRAM_AGAIN_CODE :
                               CHECK_PROGRAM_AGAIN_NONE;
            }
        }
        if (active == 1 || again != CHECK_PROGRAM_AGAIN_NONE)
        {
            arena_pop_to(output, output_before);
            arena_pop_to(bytes, bytes_before);
            arena_pop_to(scratch, scratch_before);
            memset(run.lane_states, 0, sizeof(CheckEmitLane));
            check_program_emit_alone(&run);
        }
    }
    const uint64_t lanes_end = timing ? check_program_lanes_now() : 0;
    if (lanes != NULL)
    {
        lanes->active = active;
        lanes->parts = run.part_count;
        lanes->again = again;
        lanes->prepare_ns = lanes_begin - started;
        lanes->lanes_ns = lanes_end - lanes_begin;
    }
    // The lowering's result, in source order's order (check_lower_finish): its first failure, or its label symbols, its
    // runtime functions' symbols and its entries.
    CheckLowerPart* parts = valid && run.part_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPart, run.part_count) : 0;
    valid = valid && (run.part_count == 0 || parts != 0);
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        parts[p] = run.parts[p].lower;
    }
    if (valid && !check_lower_finish(&result->lowered, &shared, parts, run.part_count))
    {
        valid = false;
    }
    if (lowering && result->lowered.status != CHECK_PROGRAM_OK)
    {
        check_lower_failed(&result->lowered, output, mark);
        result->status = result->lowered.status;
        result->token = result->lowered.token;
        result->backend_node = result->lowered.backend_node;
        result->reason = result->lowered.reason;
    }
    else if (valid)
    {
        result->lowered.count = count;
        result->lowered.output_bytes = output->length - mark;
    }
    for (uint32_t i = 0; valid && i < program->input->graph.overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = program->input->graph.overload_candidates + i;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && candidate->object != 0 && candidate->object < program->object_count &&
            program->object_symbols[candidate->object] != 0)
        {
            // A typed overload selection does not supply the required ABI symbol mangling.
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "overloadable functions (their mangled names)";
            result->token = candidate->declaration;
            valid = false;
        }
    }
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    String8* symbol_names = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + 1) : 0;
    uint8_t* retained_objects = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->object_count) : 0;
    result->function_atoms = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, count) : 0;
    valid = valid && alignments != 0 && symbol_names != 0 && retained_objects != 0 && (count == 0 || result->function_atoms != 0);
    if (valid)
    {
        memset(alignments, 0, (uint64_t)program->symbol_count + 1);
        memset(symbol_names, 0, ((uint64_t)program->symbol_count + 1) * sizeof(String8));
        memset(retained_objects, 0, program->object_count);
        for (uint32_t token = 0; token < program->input->token_count; token += 1)
        {
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && object != 0 && object < program->object_count)
            {
                bool gnu_inline = false;
                retained_objects[object] |= check_program_attribute_roots(program->input->attributes[token],
                    program->input->declaration_kinds[token] == C_BINDING_FUNCTION, &gnu_inline) != 0;
            }
        }
        for (uint32_t token = 0; valid && token < program->input->token_count; token += 1)
        {
            const CheckInputAttributes* attributes = program->input->attributes[token];
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && attributes != 0 && object != 0 && object < program->object_count)
            {
                const SymbolId symbol = program->object_symbols[object];
                alignments[symbol] = attributes->alignment_log2 > alignments[symbol] ? attributes->alignment_log2 : alignments[symbol];
                for (uint32_t i = 0; valid && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    if (symbol != 0 && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !program->input->registers[token])
                    {
                        const String8 previous = symbol_names[symbol];
                        valid = previous.length == 0 || (previous.length == attribute->text.length &&
                                memcmp(previous.pointer, attribute->text.pointer, previous.length) == 0);
                        if (valid)
                        {
                            symbol_names[symbol] = attribute->text;
                        }
                        else
                        {
                            result->status = CHECK_PROGRAM_INVALID;
                            result->token = attribute->name_token;
                        }
                    }
                    const CheckInputAttributeKind kind = attribute->kind;
                    const bool alias = kind == CHECK_ATTRIBUTE_ALIAS || kind == CHECK_ATTRIBUTE_IFUNC;
                    const bool visible_alias = alias && (symbol != 0 || program->input->linkage[token] == 2 || retained_objects[object]);
                    const bool unpublished = kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
                                             kind == CHECK_ATTRIBUTE_WEAK || kind == CHECK_ATTRIBUTE_WEAKREF || kind == CHECK_ATTRIBUTE_RETAIN ||
                                             kind == CHECK_ATTRIBUTE_SECTION || kind == CHECK_ATTRIBUTE_COMMON ||
                                             (kind == CHECK_ATTRIBUTE_VISIBILITY && !check_input_attribute_equal(attribute->text, "default"));
                    if (!attribute->ignored && (visible_alias || (symbol != 0 && unpublished)))
                    {
                        // The typed source contract retains these obligations. Until symbol aliases and startup
                        // lists, section properties and weak/visibility semantics are published, refusing code
                        // emission is required. A dropped definition has no object-format obligation of its own.
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                        result->reason = "this attribute (alias, ifunc, constructor, destructor, weak, retain, section, common or a visibility)";
                        result->token = attribute->name_token;
                        valid = false;
                    }
                }
            }
        }
    }
    // Tier 0's failures, the first body's: its own refusals are features it doesn't have yet (an entry without a
    // template, or values past its depth tables that its deep slots don't take: tier0_select_deep).
    uint32_t failed = count;
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        failed = run.parts[p].tier0_failed < failed && run.parts[p].tier0_failed != run.parts[p].lower.end ? run.parts[p].tier0_failed : failed;
    }
    if (valid && failed != count)
    {
        const Tier0Code* code = run.codes + failed;
        result->status = code->error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE :
                         code->error == TIER0_ERROR_UNSUPPORTED || code->error == TIER0_ERROR_DEPTH ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
        result->reason = code->error == TIER0_ERROR_DEPTH ? "more values live at once than Tier 0 has registers for, with long double values or "
                                                            "a call's aggregate arguments, or than its deep slots take" :
                         code->error != TIER0_ERROR_UNSUPPORTED ? NULL : "this operation in Tier 0";
        result->backend_error = code->error;
        result->backend_node = code->node;
        result->token = program->bodies[failed].declaration;
        valid = false;
    }
    uint64_t references = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        references += run.codes[b].reference_count;
        result->records += run.codes[b].count;
        result->deferred += run.codes[b].deferred;
    }
    // A failure before the rows' room leaves `rows` to the failure's reset below; the rows themselves are the bodies',
    // each body's after the ones before it, up to the first body whose code failed after Tier 0 (whose failure the
    // emission reports, unless the link's or its binding's comes first), past which no body has its code.
    uint32_t encoded = count;
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        const uint32_t code_failed = run.parts[p].code_failed;
        encoded = code_failed != run.parts[p].lower.end && code_failed < encoded ? code_failed : encoded;
    }
    if (valid && rows != NULL)
    {
        valid = check_program_emit_reserve_rows(rows, result->lowered.entries, count, output);
        for (uint32_t b = 0; valid && b < encoded; b += 1)
        {
            const CheckEmitBody* body = run.bodies + b;
            rows->first[b] = rows->count;
            rows->counts[b] = body->row_count;
            rows->sizes[b] = (uint32_t)body->length;
            rows->bodies[b] = body->body_offset;
            memcpy(rows->offsets + rows->count, body->row_offsets, (uint64_t)body->row_count * sizeof(uint32_t));
            memcpy(rows->tokens + rows->count, body->row_tokens, (uint64_t)body->row_count * sizeof(uint32_t));
            rows->count += body->row_count;
        }
    }
    const CheckProgramHelpers* helpers = &result->lowered.helpers;
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        references += helpers->items[h].code.reference_count;
        result->records += helpers->items[h].code.count;
        result->deferred += helpers->items[h].code.deferred;
    }
    // An executable's link has room for the runtime's records too: the crt objects', and the archives' members' that the
    // names the unit refers to without defining them may load (bind_capacity_needed), its symbols' and the runtime
    // functions' the lowering calls. With unwind tables, it has room for their EH_FRAME atom and a fixup per function.
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    String8* externals = binding != NULL && valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + result->lowered.runtime_symbol_count + 1) : 0;
    uint32_t external_count = 0;
    valid = valid && (binding == NULL || externals != 0);
    for (uint32_t s = 0; valid && externals != 0 && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        if (declaration != BUSTER_CHECK_NONE && !program->input->compound_objects[declaration] && !defined[object] && program->input->linkage[declaration] == 2)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            externals[external_count++] = symbol_names[s + 1].length != 0 ? symbol_names[s + 1] :
                                                                              (String8){ .pointer = (char*)intern_bytes(identifiers, identifier),
                                                                                         .length = intern_length(identifiers, identifier) };
        }
    }
    for (uint32_t r = 0; valid && externals != 0 && r < result->lowered.runtime_symbol_count; r += 1)
    {
        const char* spelling = check_lower_runtime_names[result->lowered.runtime_order[r]];
        externals[external_count++] = (String8){ .pointer = (char*)spelling, .length = strlen(spelling) };
    }
    const LinkCapacity runtime = binding != NULL && valid ? bind_capacity_needed(binding->runtime, externals, external_count, scratch, &name_bytes, &atom_bytes) :
                                                            (LinkCapacity){ 0 };
    const uint64_t atom_count = (uint64_t)data_capacity.atoms + count + helpers->count + runtime.atoms + (frames != 0 ? 1 : 0);
    const uint64_t fixup_count = (uint64_t)data_capacity.fixups + references + runtime.fixups + frames;
    const uint64_t wide_count = (uint64_t)data_capacity.wide_addends + references + runtime.wide_addends;
    const uint64_t symbol_count = (uint64_t)program->symbol_count + helpers->count + result->lowered.runtime_symbol_count + runtime.symbols;
    const uint64_t name_count = (uint64_t)program->symbol_count + helpers->count + result->lowered.runtime_symbol_count + runtime.names;
    valid = valid && atom_count < UINT32_MAX && fixup_count < UINT32_MAX && wide_count < UINT32_MAX && symbol_count < UINT32_MAX &&
            name_count < UINT32_MAX && (uint64_t)data_capacity.priorities + runtime.priorities < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = valid ? (uint32_t)name_count : 0,
        .atoms = valid ? (uint32_t)atom_count : 0,
        .symbols = valid ? (uint32_t)symbol_count : 0,
        .fixups = valid ? (uint32_t)fixup_count : 0,
        .wide_addends = valid ? (uint32_t)wide_count : 0,
        .priorities = valid ? data_capacity.priorities + runtime.priorities : 0,
    };
    valid = valid && link_initialize(&result->link, output, output, capacity);
    // A block-scope static's declaration is in a body (declaration_bodies) and has no linkage; a block-scope extern or
    // function declaration has linkage, and its symbol is the file-scope entity's.
    const CheckInput* input = program->input;
    // Only a unit that makes a name, which most don't, needs the table of the names a made one must not be, and the
    // source's names in it first; the others add their names as they come.
    bool makes = false;
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        makes |= declaration != BUSTER_CHECK_NONE && check_program_emit_makes_name(input, declaration, symbol_names[s + 1]);
    }
    uint32_t slots = 2;
    while (makes && slots <= (uint64_t)program->symbol_count * 2 && slots < (UINT32_C(1) << 31))
    {
        slots *= 2;
    }
    CheckProgramNames names = { .mask = slots - 1 };
    names.taken = valid && makes ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, slots) : 0;
    NameId* given = valid && makes ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, program->symbol_count) : 0;
    valid = valid && (!makes || (names.taken != 0 && given != 0));
    if (names.taken != 0)
    {
        memset(names.taken, 0, (uint64_t)slots * sizeof(NameId));
    }
    // The names the source gives come first, so that no made name is one of them.
    for (uint32_t s = 0; valid && makes && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        const String8 renamed = symbol_names[s + 1];
        const bool made = declaration != BUSTER_CHECK_NONE && check_program_emit_makes_name(input, declaration, renamed);
        given[s] = 0;
        if (declaration != BUSTER_CHECK_NONE && !made)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            given[s] = renamed.length != 0 ? check_program_emit_given_name(&result->link, &names, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length) :
                                             check_program_emit_given_name(&result->link, &names, intern_bytes(identifiers, identifier),
                                                                           intern_length(identifiers, identifier));
            valid = given[s] != 0;
        }
    }
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        NameId name = 0;
        uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            const String8 renamed = symbol_names[s + 1];
            const uint32_t opening = input->declaration_bodies[declaration];
            if (input->compound_objects[declaration])
            {
                name = check_program_emit_made_name(&result->link, &names, (const uint8_t*)".compoundliteral", sizeof(".compoundliteral") - 1, scratch);
            }
            else if (renamed.length == 0 && opening != BUSTER_CHECK_NONE && input->linkage[declaration] == 0)
            {
                // The function's name: its symbol's asm label, or its identifier (--lower-all selects the statics of
                // an inline-only function, which has no symbol, and then no label).
                const uint32_t function = opening < input->token_count ? input->body_declarations[opening] : BUSTER_CHECK_NONE;
                const uint32_t function_object = function != BUSTER_CHECK_NONE ? input->objects[function] : 0;
                const SymbolId function_symbol = function_object != 0 && function_object < program->object_count ? program->object_symbols[function_object] : 0;
                const String8 label = function_symbol != 0 ? symbol_names[function_symbol] : (String8){ 0 };
                const uint32_t function_identifier = function != BUSTER_CHECK_NONE ? parser->tokens.names[function] : 0;
                const uint8_t* context = label.length != 0 ? (const uint8_t*)label.pointer : intern_bytes(identifiers, function_identifier);
                const uint32_t context_length = label.length != 0 ? (uint32_t)label.length : intern_length(identifiers, function_identifier);
                const uint32_t own_length = intern_length(identifiers, identifier);
                const uint64_t base_length = (uint64_t)context_length + 1 + own_length;
                uint8_t* base = function != BUSTER_CHECK_NONE && base_length < UINT32_MAX - 12 ? arena_push(scratch, base_length, 1) : 0;
                if (base != 0)
                {
                    memcpy(base, context, context_length);
                    base[context_length] = '.';
                    memcpy(base + context_length + 1, intern_bytes(identifiers, identifier), own_length);
                    name = check_program_emit_made_name(&result->link, &names, base, (uint32_t)base_length, scratch);
                }
            }
            else if (given != 0)
            {
                name = given[s];
            }
            else
            {
                name = renamed.length != 0 ? link_add_name(&result->link, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length) :
                                             link_add_name(&result->link, intern_bytes(identifiers, identifier), intern_length(identifiers, identifier));
            }
            const uint8_t kind = program->input->declaration_kinds[declaration] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 program->input->thread_local[declaration] ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            flags = kind | (program->input->linkage[declaration] == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL);
        }
        else if (object != 0 && object - 1 < parser->tokens.count &&
                 (parser->tokens.kinds[object - 1] == PARSE_KIND_STRING || check_input_function_name_token(parser, object - 1)))
        {
            // A string literal's, or a function name's array (__func__, __FUNCTION__, __PRETTY_FUNCTION__), which clang
            // makes a private constant as it makes a literal's.
            char spelling[48];
            const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_string_%u", object);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        }
        else if (object != 0 && object - 1 < parser->tokens.count && parser->tree.roles[object - 1] == PARSE_ROLE_LABEL)
        {
            // A label whose address the program takes (GNU C's &&label): a place in its function's code, defined below.
            char spelling[48];
            const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_label_%u", object);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
            flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE;
        }
        else
        {
            result->status = CHECK_PROGRAM_INVALID;
            result->token = object != 0 ? object - 1 : 0;
        }
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == s + 1;
    }
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        const CheckProgramHelper* helper = helpers->items + h;
        char spelling[64];
        // An asm statement's helper is named after its statement (c_check_program_asm.c), a builtin's after its builtin.
        const int length = helper->assembly != 0 ? snprintf(spelling, sizeof(spelling), ".Lbuster_asm_%u", helper->assembly - 1) :
                                                   snprintf(spelling, sizeof(spelling), ".Lbuster_builtin_%u_%u", helper->id, helper->bytes);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION,
                                            BUSTER_LINK_SOURCE_NONE) == helper->symbol;
    }
    // The runtime's functions the lowering made symbols of its own for, in their order (CheckProgramLowered.runtime_order).
    for (uint32_t r = 0; valid && r < result->lowered.runtime_symbol_count; r += 1)
    {
        const CheckLowerRuntime runtime = (CheckLowerRuntime)result->lowered.runtime_order[r];
        const char* spelling = check_lower_runtime_names[runtime];
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)strlen(spelling));
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_NONE) ==
                                 result->lowered.runtime_symbols[runtime];
    }
    // Every symbol of the program's exists: an executable's runtime joins now, before the program's atoms, with the
    // symbols this emission and the static data will define marked as the program's definitions (bind.c).
    for (uint32_t b = 0; valid && binding != NULL && b < count + helpers->count + binding->data_count; b += 1)
    {
        const uint32_t object = b < count ? program->bodies[b].object : b >= count + helpers->count ? binding->data_objects[b - count - helpers->count].object : 0;
        const SymbolId symbol = b >= count && b < count + helpers->count ? helpers->items[b - count].symbol :
                                object < program->object_count ? program->object_symbols[object] : 0;
        if (symbol != 0 && symbol < result->link.symbols.count)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    for (uint32_t k = 0; valid && binding != NULL && k < result->lowered.label_symbol_count; k += 1)
    {
        result->link.symbols.source[result->lowered.label_symbols[k]] = BUSTER_LINK_SOURCE_PROGRAM;
    }
    if (valid && binding != NULL && !bind_begin(binding->bind, &result->link, binding->runtime, binding->path, bytes, 1, binding->arena))
    {
        const NameId name = binding->bind->name;
        if (name != 0 && name < result->link.names.count)
        {
            snprintf(binding->failed_name, sizeof(binding->failed_name), "%.*s", (int)result->link.names.length[name],
                     (const char*)result->link.names.bytes[name]);
        }
        result->status = binding->bind->error == BIND_ERROR_FULL ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        result->reason = "binding against the C runtime failed";
        valid = false;
    }
    // Every reference of a runtime function's provisional symbol takes the function's (check_lower_finish numbered them):
    // only a part that called one has them.
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        for (uint32_t b = run.parts[p].lower.first; run.parts[p].lower.runtime_count != 0 && b < run.parts[p].lower.end; b += 1)
        {
            const Tier0Code* code = run.codes + b;
            for (uint32_t i = 0; i < code->reference_count; i += 1)
            {
                const uint64_t provisional = (uint64_t)code->references[i].target - shared.provisional_runtimes;
                code->references[i].target = provisional < CHECK_LOWER_RUNTIMES ? result->lowered.runtime_symbols[provisional] : code->references[i].target;
            }
        }
    }
    // The bodies' atoms, symbols and fixups in source order, each body's code as its lane made it, then the helpers', which
    // are encoded here: the labels' symbols, each function's after the ones before it (CheckProgramLowered.label_symbols),
    // and with unwind tables each function's frame, as its code is sized.
    uint32_t labels = 0;
    CheckEmitBody helper_body;
    for (uint32_t b = 0; valid && b < count + helpers->count; b += 1)
    {
        const bool source_body = b < count;
        const Tier0Code* code = source_body ? &run.codes[b] : &helpers->items[b - count].code;
        const SymbolId symbol = source_body ? program->object_symbols[program->bodies[b].object] : helpers->items[b - count].symbol;
        const uint8_t alignment = source_body && alignments[symbol] > 5 ? alignments[symbol] - 1 : 4;
        const uint64_t work_mark = scratch->length;
        CheckEmitBody* body = source_body ? run.bodies + b : &helper_body;
        if (!source_body)
        {
            // A helper's code, encoded on the calling thread as a body's is on its lane, its frame after the bodies'.
            helper_body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK };
            uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
            body->fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
            const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
            const uint64_t before = bytes->length;
            uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
            bool encodes = offsets != 0 && body->fixup_ends != 0 && encoded != 0;
            body->status = encodes ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
            body->token = BUSTER_CHECK_NONE;
            if (encodes && verify && x86_validate(code->records, code->count) != code->count)
            {
                body->status = CHECK_PROGRAM_BACKEND;
                body->reason = "internal compiler error: a record of Tier 0's fails x86_validate";
                encodes = false;
            }
            if (encodes)
            {
                const BranchSizingCheck shape = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
                const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
                const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                                       scalar ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                                x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
                encodes = length >= 0;
                if (encodes && unwind)
                {
                    encodes = length <= INT32_MAX && unwind_tier0_frame(code, offsets, (uint32_t)length, &run.frames[b]) == UNWIND_OK;
                    UnwindFunction proven;
                    const UnwindError analyzed = !encodes || !verify ? UNWIND_ERROR_UNSUPPORTED :
                                                 scalar ? unwind_tier0_scalar(code, offsets, (uint32_t)length, &proven) :
                                                          unwind_tier0(code, offsets, (uint32_t)length, &proven);
                    encodes = encodes && (analyzed != UNWIND_OK || memcmp(&proven, &run.frames[b], sizeof(proven)) == 0);
                    body->reason = encodes ? body->reason : "internal compiler error: a function's prologue or epilogue isn't the frame its unwind table describes";
                }
                if (encodes)
                {
                    const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length;
                    arena_pop_to(bytes, end > before ? end : before);
                    body->bytes = encoded;
                    body->length = (uint64_t)length;
                }
                else
                {
                    body->status = CHECK_PROGRAM_BACKEND;
                }
            }
        }
        if (body->status != CHECK_PROGRAM_OK)
        {
            result->status = body->status;
            result->reason = body->reason != NULL ? body->reason : result->reason;
            result->token = body->status == CHECK_PROGRAM_RESOURCE ? result->token : body->token;
            valid = false;
        }
        if (valid)
        {
            const AtomId atom = link_add_atom(&result->link, BUSTER_LINK_CLASS_TEXT, alignment, body->length, body->bytes, 0);
            valid = atom != 0 && symbol != 0 && link_define_symbol(&result->link, symbol, atom, 0, body->length) &&
                    link_add_code_fixups(&result->link, atom, code->records, code->count, body->fixup_ends, code->reference_count, code->references, code->reference_count);
            // Each label at its first record's offset, after branch sizing: the program's definition.
            for (uint32_t k = 0; valid && source_body && k < code->label_count; k += 1)
            {
                valid = labels < result->lowered.label_symbol_count && body->label_offsets[k] != UINT32_MAX &&
                        link_define_symbol(&result->link, result->lowered.label_symbols[labels], atom, body->label_offsets[k], 0);
                if (valid)
                {
                    result->link.symbols.source[result->lowered.label_symbols[labels]] = BUSTER_LINK_SOURCE_PROGRAM;
                }
                labels += 1;
            }
            if (valid)
            {
                result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
            }
            if (source_body)
            {
                result->function_atoms[b] = atom;
            }
            result->code_bytes += body->length;
        }
        if (!source_body)
        {
            arena_pop_to(scratch, work_mark);
        }
    }
    // The unwind tables, after the last function: measured, then written into the byte arena, one CIE and an FDE per
    // function after it, as one EH_FRAME atom whose FDEs' initial locations refer to their functions.
    if (valid && frames != 0)
    {
        UnwindOutput tables = { .location_capacity = (uint32_t)frames, .shared = true };
        valid = unwind_write(run.frames, (uint32_t)frames, &tables, scalar) == UNWIND_ERROR_CAPACITY;
        uint32_t* locations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, frames) : 0;
        uint8_t* table_bytes = valid ? arena_push(bytes, tables.size, 8) : 0;
        tables.bytes = table_bytes;
        tables.capacity = tables.size;
        tables.locations = locations;
        valid = locations != 0 && table_bytes != 0 && unwind_write(run.frames, (uint32_t)frames, &tables, scalar) == UNWIND_OK;
        const AtomId atom = valid ? link_add_atom(&result->link, BUSTER_LINK_CLASS_EH_FRAME, 3, tables.size, table_bytes, 0) : 0;
        valid = atom != 0;
        for (uint32_t f = 0; valid && f < frames; f += 1)
        {
            const SymbolId symbol = f < count ? program->object_symbols[program->bodies[f].object] : helpers->items[f - count].symbol;
            valid = link_add_fixup(&result->link, atom, locations[f], BUSTER_LINK_FIXUP_PC32, symbol, 0) != 0;
        }
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : result->status;
        const uint32_t token = result->token;
        const Tier0Error backend = result->backend_error;
        const uint32_t backend_node = result->backend_node;
        const char* reason = result->reason;
        arena_pop_to(output, mark);
        arena_pop_to(bytes, byte_mark);
        *result = (CheckProgramCode){ .status = status, .token = token, .backend_error = backend, .backend_node = backend_node, .reason = reason };
        if (rows != NULL)
        {
            *rows = (CheckProgramRows){ 0 };
        }
        for (uint32_t lane = 1; lanes != NULL && lane < BUSTER_CHECK_EMIT_MAX_LANES; lane += 1)
        {
            arena_pop_to(&lanes->arenas[lane].output, 0);
            arena_pop_to(&lanes->arenas[lane].scratch, 0);
            arena_pop_to(&lanes->arenas[lane].bytes, 0);
        }
    }
    arena_pop_to(scratch, temporary);
    if (timing)
    {
        lanes->merge_ns = check_program_lanes_now() - lanes_end;
    }
    return valid;
}

// check_program_emit_lanes on the calling thread alone.
static bool check_program_emit(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                               const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                               CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify)
{
    return check_program_emit_lanes(result, program, parser, identifiers, data_capacity, scalar, output, bytes, scratch, binding, rows, unwind, verify, NULL);
}
