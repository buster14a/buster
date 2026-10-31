// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real consumer of CheckProgram: lower, validate, compile, size branches, encode, and publish code atoms and
// their symbolic fixups in the Link shared with static data. check_program assigns source object symbols;
// lowering appends helper and explicit target-runtime symbols before any atom/fixup publication.
// data_capacity reserves the data consumer's atoms/fixups; that consumer defines its already-created symbols.
// The caller performs final link_check after both publications. Output tables/names and byte arenas are distinct
// from scratch; all three roll back on failure. Backend template initialization precedes concurrent workers, only
// when the selected program has a body: its helpers also come only from those bodies. Data-only links need no tables.
// With `unwind`, every function's unwind table too (#395): its frame as Tier 0 published it (unwind_tier0_frame, and
// its tail calls' teardowns with unwind_tier0_frame_tails, #147), at
// its records' ends after branch sizing, written after the last function (unwind_write) as one EH_FRAME atom, a CIE
// and an FDE per function after it, each FDE's initial location a PC32 fixup to its function's symbol; with `verify`,
// each the analyzer's proof from the records too, where the analyzer takes the function (unwind_tier0).

#include "worker_budget.c"

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
    // #619: the file-scope assembly's atoms (check_program_emit_file_asm), in the link before the functions', and each
    // one's section, which -c's object gives it (check_program_sections).
    AtomId* asm_atoms;
    ObjectAtomSection* asm_sections;
    uint32_t asm_atom_count;
    // GNU's retain attribute (#147): the symbols of the program whose declarations have one before their definitions, in
    // symbol order, whose definitions -c's object puts in sections of their own with SHF_GNU_RETAIN
    // (check_program_sections); none in a unit without one.
    SymbolId* retained_symbols;
    uint32_t retained_count;
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

// Adds `spelling`, the name the source gives a symbol, to the link's names, and to the names a made one must not be if
// it could be one: its NameId, or 0 when the link has no room. Every made name has a '.', which no identifier has, so
// only an asm label with one can be a made name, and only it goes in the table (#442: most units' given names are
// identifiers, whose probes the table then doesn't pay for).
static NameId check_program_emit_given_name(Link* link, CheckProgramNames* names, const uint8_t* spelling, uint32_t length, bool label)
{
    const uint64_t hash = link_hash(spelling, length);
    uint32_t slot = 0;
    const bool dotted = label && memchr(spelling, '.', length) != 0;
    const bool taken = dotted && check_program_emit_taken(link, names, spelling, length, hash, &slot);
    const NameId result = link_add_name_hashed(link, spelling, length, hash);
    if (dotted && !taken && result != 0)
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
//
// With several source files the units run on lanes (c_checker_units.c), each unit on one, where a unit's emission can't
// run lanes of its own: lane_run refuses a group inside the units' group. The units' lanes are a board instead (#777,
// CheckEmitBoard): a unit's emission that starts while one of them has no unit left to take cuts its bodies into parts
// as lanes of its own would (parts_by_cost), shares them on its lane's share and takes them itself, and the lanes that
// take no units take them too (check_program_emit_help), each with a writer and arenas of its own in that emission,
// until no lane takes units. The parts are the same steps, merged the same way, so the outputs are one lane's.

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

// A lane's share on the units' board (#777): the emission its unit shares, one at a time, each an epoch of the share.
// The epoch's next part to take (low 32 bits of `ticket`) and its parts' count (low 32 bits of `limit`), each beside the
// epoch (their high 32 bits): a take is a compare-and-swap of the ticket, whose epoch must be the limit's, so that a lane
// that read an epoch's ticket takes none of the next's. Its owner sets the epoch's `run` and `order` (its parts, the
// costliest first) before its ticket. Each part finishes every access to the run before incrementing `done`; the last
// publishes `complete` (lane_publish, lane_wait), which the owner acquires before its emission goes on or the share
// opens another epoch. The count alone is not that handoff: the last part may not have published complete yet.
typedef struct CheckEmitShare CheckEmitShare;
struct CheckEmitShare
{
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t ticket;
    uint64_t limit;
    uint32_t done;
    uint32_t complete;
    struct CheckEmitRun* run;
    const uint32_t* order;
};

// The units' lanes as one board (#777): how many there are, how many still take units (a lane that takes no more helps,
// check_program_emit_help, until none takes any), the word that advances (lane_advance) when a share opens and when a
// lane stops taking units, which the helping lanes wait on, the parts that lanes other than the owner's took (a test's
// count), and a share per lane.
typedef struct CheckEmitBoard CheckEmitBoard;
struct CheckEmitBoard
{
    uint32_t lanes;
    uint32_t taking;
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t generation;
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t others;
    CheckEmitShare shares[BUSTER_LANE_MAX_COUNT];
};

// A unit's emission in its command's flow (#905, the C driver's), a LaneFlow of the session's lanes: lane 0 runs the
// unit's stages alone, and each of its emissions that pays for more than one lane is a round of the bodies
// (BUSTER_CHECK_EMIT_ROUND_BODIES), which lane 0 announces with the emission's lanes (check_program_emit_round): the
// first of them, lane 0 among them, take the bodies' parts (check_program_emit_lane), and the others follow the rounds
// (check_program_emit_follow) until lane 0 ends the flow (lane_flow_end).
#define BUSTER_CHECK_EMIT_ROUND_BODIES 1u

// The C driver's --tier1 (c_check_tier1.c, #138's piece for #140): each body Tier 1's pipeline takes compiled by it,
// every other by Tier 0. A lane's arenas for the pipeline: each step's output and the steps' scratch, reserved at the
// lane's first body and emptied for each, and the forms the lane's last target allows (X86_FORM_COUNT entries).
#define BUSTER_CHECK_TIER1_STEPS 6
typedef struct CheckTier1Lane CheckTier1Lane;
struct CheckTier1Lane
{
    Arena steps[BUSTER_CHECK_TIER1_STEPS];
    Arena scratch;
    Arena forms;
    uint8_t* allowed;
    X86Target target;
    bool reserved;
    bool targeted;
};

// What became of a body under --tier1: Tier 1's code, or the step that refused it, whose body Tier 0 compiled. A
// selection's refusal also names the IR operation it refused, or the function (IR_OP_COUNT) where it refused no
// operation, or its error past that (CheckTier1Report.operations); an emission's, its error (.emission).
typedef enum CheckTier1Stage
{
    CHECK_TIER1_COMPILED,
    CHECK_TIER1_NAKED,
    CHECK_TIER1_LOWERING,
    CHECK_TIER1_PROMOTION,
    CHECK_TIER1_OPTIMIZATION,
    CHECK_TIER1_ANALYSIS,
    CHECK_TIER1_SELECTION,
    CHECK_TIER1_EMISSION,
    CHECK_TIER1_RESOURCE,
    CHECK_TIER1_STAGE_COUNT,
} CheckTier1Stage;

// What Tier 1's code of a body holds: its records, and the allocation's traffic, its spills' stores and reloads, the
// saves and restores around its calls and fixed registers' sequences, its recomputed values (constants and addresses,
// #178), the definitions of recomputed values it left out, which nothing read, and its edges' moves; its spill slots, and
// its frame's bytes (Tier1Frame.maximum_stack_bytes: what it takes below the return address, its saved registers among
// them).
typedef struct CheckTier1Counts CheckTier1Counts;
struct CheckTier1Counts
{
    uint32_t records;
    uint32_t spill_stores;
    uint32_t spill_loads;
    uint32_t call_stores;
    uint32_t call_loads;
    uint32_t rematerializations;
    uint32_t dropped_definitions;
    uint32_t edge_moves;
    uint32_t spill_slots;
    uint32_t frame_bytes;
};

#define BUSTER_CHECK_TIER1_OPERATIONS 256
typedef struct CheckTier1Report CheckTier1Report;
struct CheckTier1Report
{
    uint32_t functions;
    uint32_t stages[CHECK_TIER1_STAGE_COUNT];
    uint32_t operations[BUSTER_CHECK_TIER1_OPERATIONS];
    uint32_t emission[BUSTER_CHECK_TIER1_OPERATIONS];
    // The counts of Tier 1's bodies, summed.
    uint64_t records;
    uint64_t spill_stores;
    uint64_t spill_loads;
    uint64_t call_stores;
    uint64_t call_loads;
    uint64_t rematerializations;
    uint64_t dropped_definitions;
    uint64_t edge_moves;
    uint64_t spill_slots;
    uint64_t frame_bytes;
};

// A lane's arenas past the first lane's, which are the caller's: its functions' lowered columns (`output`), its writer's
// columns, its functions' Tier 0 code and what their fixups and rows need until the merge (`scratch`), and its
// functions' bytes (`bytes`); and with --tier1, every lane's (the first's too) for Tier 1's pipeline.
typedef struct CheckProgramLaneArenas CheckProgramLaneArenas;
struct CheckProgramLaneArenas
{
    Arena output;
    Arena scratch;
    Arena bytes;
    CheckTier1Lane tier1;
};

// How check_program_emit_lanes runs a unit's functions on lanes, and what it ran. The caller keeps it until it is done
// with the emission's link and lowering: the atoms' bytes and the functions' columns of the lanes past the first are in
// their arenas here (check_program_lanes_release), which the next emission with it reuses, as it reuses the first's
// scratch for lane 0's writer's columns (CheckEmitRun.columns) while it has their room.
typedef struct CheckProgramLanes CheckProgramLanes;
struct CheckProgramLanes
{
    // The most lanes to run (0 or 1 for one), and whether to run all of them, as long as there are bodies for them,
    // whatever the bodies' tokens (the tests'); the lanes' CPUs for lane_run, or 0.
    uint32_t lanes;
    bool every_lane;
    const uint32_t* cpus;
    // Optional external ownership for the extra workers. The C driver supplies it; isolated callers/tests may omit it.
    WorkerBudget* budget;
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
    // With several source files (#777), the units' lanes' board, on which the emission shares its parts with the lanes
    // that have no unit left to take rather than run lanes of its own, and its lane's place there; and a test's pause
    // after each part the sharing lane takes itself, in microseconds, so that the other lanes take parts. Their lanes'
    // states and arenas are their places plus one: the first is the sharing lane's, with the caller's arenas.
    CheckEmitBoard* board;
    uint32_t board_lane;
    uint32_t board_pause_microseconds;
    // In a command's flow (#905), its rounds' rendezvous: the emission's lanes are rounds of the flow's lanes rather
    // than a lane_run of its own. Null elsewhere.
    LaneFlow* flow;
    // In a command of several units' flow (#905, c_checker_units.c), the rounds of the units' run, the link's merge,
    // check and write. Null elsewhere.
    struct CheckUnitsFlow* units_flow;
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
    // --tier1, and what became of the emissions' bodies, summed over them; with --tier1-refusals, the stream that each
    // body Tier 0 compiled gets a line on, its name and the step that refused it (check_tier1_print_refusal). With
    // --tier1-no-rematerialize, Tier 1's allocation spills what it would recompute (#178's comparison).
    bool tier1;
    bool tier1_no_rematerialize;
    CheckTier1Report tier1_report;
    FILE* tier1_refusals;
    CheckProgramLaneArenas arenas[BUSTER_CHECK_EMIT_MAX_LANES];
};

// Whether --tier1's tables are built (check_program_emit_timed), once per process.
static bool check_tier1_ready = false;

// Defined in c_check_tier1.c, after Tier 1's modules: a body's code by Tier 1's pipeline on `lane`'s arenas, from its
// typed stream `function` whose result has the typed class `result_class` (check_program_result_class), its records,
// branch columns and references in `output`, into *code (with `scalar` each step's twin). Returns the body's stage:
// CHECK_TIER1_COMPILED, or the step that refused it (with a selection's refused IR operation in *operation), leaving
// `output` as it was.
static CheckTier1Stage check_tier1_compile(CheckTier1Lane* lane, const Tier0Function* function, uint8_t result_class, const X86Target* target, bool scalar,
                                           bool rematerialize, Arena* output, Tier0Code* code, uint32_t* operation, CheckTier1Counts* counts);
// Releases a lane's arenas for the pipeline.
static void check_tier1_lane_release(CheckTier1Lane* lane);
// Prints `report`, a line on `stream`: how many functions each tier compiled, and why Tier 1 refused the others.
static void check_tier1_print_report(FILE* stream, const CheckTier1Report* report);
// Prints a line on `stream` for a function Tier 1 refused: its name, `length` bytes at `name`, and its outcome's step and
// IR operation or error.
static void check_tier1_print_refusal(FILE* stream, const uint8_t* name, uint32_t length, uint32_t outcome);

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
        check_tier1_lane_release(&lanes->arenas[lane].tier1);
    }
}

// A body's code, as its lane made it (check_program_emit_code): its bytes, encoded after branch sizing; each marked
// instruction's end (x86_encode's fixup ends) and each label's offset (or UINT32_MAX for a record past the code), and a
// naked function's, each of its templates' labels' (CheckProgramNaked.labels), for the merge's link; with -g its raw
// rows, with room for its entries, its prologue's, its epilogue's and the slack; and its failure after Tier 0, as source
// order's emission reports it (CHECK_PROGRAM_OK for none): at its declaration but for a resource's.
typedef struct CheckEmitBody CheckEmitBody;
struct CheckEmitBody
{
    uint8_t* bytes;
    uint64_t length;
    uint32_t* fixup_ends;
    uint32_t* label_offsets;
    uint32_t* asm_offsets;
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
    // The arena of its writer's columns (check_lower_writer_begin): its scratch, but lane 0's (CheckEmitRun.columns).
    Arena* columns;
    // With --tier1, its arenas for Tier 1's pipeline (CheckProgramLaneArenas.tier1); null without.
    CheckTier1Lane* tier1;
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
    // Lane 0's writer's columns, in base pages, as the other lanes' scratch is: each column has room for any of the
    // program's bodies, of which a body touches a little at the column's start, and in the caller's scratch, whose pages
    // past its first 64 KB are huge, each column's first touch cleared a huge page of its own (#531). The caller's
    // scratch where no address space can be reserved for them (check_program_emit_lanes).
    Arena* columns;
    Tier0Code* codes;
    CheckEmitBody* bodies;
    UnwindFunction* frames;
    // With --tier1, what became of each body: its CheckTier1Stage, and a refused selection's IR operation in bits 8 up;
    // and Tier 1's code's counts, for the bodies it compiled.
    uint32_t* tier1_outcomes;
    CheckTier1Counts* tier1_counts;
    uint64_t* costs;
    uint64_t* boundaries;
    uint32_t* orders[BUSTER_CHECK_EMIT_MAX_LANES];
    CheckEmitPart* parts;
    uint32_t part_count;
    CheckEmitLane* lane_states;
    // The driver's --time-stages clock (c_check_clock.c), which the calling thread's laps charge per function when it
    // compiles the bodies alone (check_program_emit_alone): null on the lanes, whose run its caller's mark charges.
    CheckClock* stage_clock;
};

// The typed class of body `b`'s result, as Tier 1's lowering takes it: its RETURNs' class, or for a function without one
// (a void function's, or one that never returns) its declared result's, UINT8_MAX for none (a naked function's).
static uint8_t check_program_result_class(const Tier0Function* function, const CheckProgram* program, uint32_t b, bool naked)
{
    uint8_t class = UINT8_MAX;
    for (uint32_t i = 0; class == UINT8_MAX && i < function->count; i += 1)
    {
        class = function->kinds[i] == BUSTER_TYPED_RETURN ? function->classes[i] : class;
    }
    if (class == UINT8_MAX && !naked)
    {
        const TypeTable* types = program->types;
        const TypeId result = type_return(types, program->input->declared_types[program->bodies[b].declaration]);
        class = check_lower_type_class(types, result);
        class = class != UINT8_MAX ? class : type_size(types, result) == 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
    }
    return class;
}

// Body `b`'s raw source rows (c_check_debug.c), from where each entry's code starts now that the branches are sized: the
// record count's offset is the end. Only -g reaches it, out of line so that the plain emission carries none of it.
static __attribute__((noinline)) void check_program_emit_body_rows(CheckEmitBody* body, const CheckProgram* program, const Parser* parser,
                                                                   const uint32_t* tokens, const Tier0Code* code, uint32_t b, uint32_t* offsets, uint32_t length,
                                                                   bool scalar)
{
    (void)scalar;

    offsets[code->count] = length;
    const uint32_t declaration = program->bodies[b].declaration;
    const uint32_t closing = parser->tree.parents[program->bodies[b].opening];
    body->row_count = BUSTER_SCALAR_PATH(scalar) ? check_debug_rows_scalar(code->entry_records, tokens, code->entry_count, offsets, length, declaration, closing,
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
    const CheckProgramNaked* naked = run->lowered->asms.naked != 0 && run->lowered->asms.naked[b].planned ? run->lowered->asms.naked + b : 0;
    *body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    // What the merge reads stays on the lane's scratch; the offsets don't.
    body->fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
    body->label_offsets = code->label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, code->label_count) : 0;
    body->asm_offsets = naked != 0 && naked->label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, naked->label_count) : 0;
    const uint64_t rows = run->sources ? (uint64_t)code->entry_count + 2 + BUSTER_CHECK_DEBUG_SLACK : 0;
    body->row_offsets = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    body->row_tokens = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    // Its tail calls' unwind events (#147, unwind_tier0_frame_tails), which the tables read once the lanes are done.
    const uint64_t tail_room = run->unwind && naked == 0 && code->tail_count != 0 ? unwind_tier0_tail_room(code) : 0;
    uint32_t* tails = tail_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, tail_room) : 0;
    const uint64_t work_mark = scratch->length;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
    const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t before = bytes->length;
    uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
    bool valid = offsets != 0 && body->fixup_ends != 0 && encoded != 0 && (code->label_count == 0 || body->label_offsets != 0) &&
                 (naked == 0 || naked->label_count == 0 || body->asm_offsets != 0) && (!run->sources || (body->row_offsets != 0 && body->row_tokens != 0)) &&
                 (tail_room == 0 || tails != 0);
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
        // The branch columns alone, which sizing reads and writes by: Tier 0's records are valid by construction, which
        // x86_validate checks above only with `verify` (#532).
        const BranchSizingCheck shape = run->scalar ? branch_sizing_validate_branches_scalar(code->records, code->count, code->branches, code->targets, code->branch_count) :
                                                      branch_sizing_validate_branches(code->records, code->count, code->branches, code->targets, code->branch_count);
        const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
        check_clock_lap(run->stage_clock, CHECK_STAGE_BRANCH_SIZING);
        const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                               run->scalar ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                             x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
        check_clock_lap(run->stage_clock, CHECK_STAGE_ENCODE);
        valid = length >= 0;
        if (valid && run->sources)
        {
            check_program_emit_body_rows(body, program, run->parser, run->lowered->entry_tokens[b], code, b, offsets, (uint32_t)length, run->scalar);
            check_clock_lap(run->stage_clock, CHECK_STAGE_DEBUG);
        }
        if (valid && run->unwind && naked != 0)
        {
            // A naked function's code is its templates', which say nothing of its frame: its unwind entry, as clang's, has
            // the call's rule alone, whatever they push.
            valid = length <= INT32_MAX;
            run->frames[b] = (UnwindFunction){ .size = (uint32_t)length };
        }
        else if (valid && run->unwind)
        {
            // A prologue or epilogue that isn't the frame Tier 0 published is the compiler's own failure; and with --verify,
            // so is a frame the analyzer proves otherwise from the records, where it takes the function (a fixed frame's,
            // and not a computed goto's).
            valid = length <= INT32_MAX && unwind_tier0_frame_tails(code, offsets, (uint32_t)length, &run->frames[b], tails) == UNWIND_OK;
            UnwindFunction proven;
            const UnwindError analyzed = !valid || !run->verify ? UNWIND_ERROR_UNSUPPORTED :
                                         run->scalar ? unwind_tier0_scalar(code, offsets, (uint32_t)length, &proven) :
                                                       unwind_tier0(code, offsets, (uint32_t)length, &proven);
            valid = valid && (analyzed != UNWIND_OK || memcmp(&proven, &run->frames[b], sizeof(proven)) == 0);
            body->reason = valid ? body->reason : "internal compiler error: a function's prologue or epilogue isn't the frame its unwind table describes";
            check_clock_lap(run->stage_clock, CHECK_STAGE_UNWIND);
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
            // A template's label may be its code's end.
            for (uint32_t k = 0; naked != 0 && k < naked->label_count; k += 1)
            {
                const uint32_t record = naked->labels[k].record;
                body->asm_offsets[k] = record < code->count ? offsets[record] : record == code->count ? (uint32_t)length : UINT32_MAX;
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
    check_clock_lap(run->stage_clock, CHECK_STAGE_CODE_ATOMS);
    return valid;
}

// Body `b`'s code by Tier 1's pipeline (--tier1, check_tier1_compile) into run->codes[b], on its lane's arenas for it,
// the code in its lane's scratch as Tier 0's is: true if the pipeline took it, and its outcome either way. A naked
// function's code is its templates'. Out of line, so that the plain emission carries none of it.
static __attribute__((noinline)) bool check_program_emit_tier1(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b, bool naked)
{
    const CheckProgram* program = run->program;
    const Tier0Function* function = &run->lowered->functions[b];
    const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
    const X86Target* target = body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? &program->target : &program->targets[body_target - 1];
    uint32_t operation = 0;
    const CheckTier1Stage stage = naked ? CHECK_TIER1_NAKED :
                                          check_tier1_compile(lane->tier1, function, check_program_result_class(function, program, b, false), target, run->scalar,
                                                              !run->lanes->tier1_no_rematerialize, lane->scratch, &run->codes[b], &operation, &run->tier1_counts[b]);
    run->tier1_outcomes[b] = (uint32_t)stage | operation << 8;
    return stage == CHECK_TIER1_COMPILED;
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
    check_clock_lap(run->stage_clock, CHECK_STAGE_LOWER);
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
        // A naked function's code is its templates' (check_program_asm_plan_naked), not its function's.
        const CheckProgramNaked* naked = run->lowered->asms.naked != 0 && run->lowered->asms.naked[b].planned ? run->lowered->asms.naked + b : 0;
        // With --tier1, Tier 1's code where its pipeline takes the body (check_program_emit_tier1).
        const bool tier1 = lane->tier1 != NULL && check_program_emit_tier1(run, lane, b, naked != 0);
        if (!tier1)
        {
            run->codes[b] = naked != 0 ? naked->code : run->scalar ? tier0_compile_scalar(function, lane->scratch) : tier0_compile(function, lane->scratch);
        }
        check_clock_lap(run->stage_clock, CHECK_STAGE_TIER0);
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

// Lane `index`'s arenas, for its first part: the caller's for lane 0, but its writer's columns' (CheckEmitRun.columns),
// and for another lane its own, reserved the first time and otherwise put back (the emission before this one's are done
// with), its scratch its writer's columns'. Returns false if one can't be reserved.
static bool check_program_emit_lane_arenas(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    bool valid = true;
    if (index == 0)
    {
        lane->output = run->output;
        lane->scratch = run->scratch;
        lane->bytes = run->bytes;
        lane->columns = run->columns;
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
        lane->columns = &arenas->scratch;
    }
    lane->tier1 = run->tier1_outcomes != NULL ? &run->lanes->arenas[index].tier1 : NULL;
    return valid;
}

// Lane `index`'s arenas and writer, for its first part.
static bool check_program_emit_lane_prepare(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    return check_program_emit_lane_arenas(run, lane, index) &&
           check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->columns);
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
// is its first part, and works on the part; then its room comes back. Lane 0's writer's columns, an arena of their own,
// are squeezed with its scratch. Out of line, so that the lanes' loop carries none of it.
static __attribute__((noinline)) void check_program_emit_squeezed(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part, uint32_t index)
{
    CheckProgramLanes* lanes = run->lanes;
    const bool arenas = lane->prepared ? lane->ready : check_program_emit_lane_arenas(run, lane, index);
    Arena* each[4] = { lane->output, lane->scratch, lane->bytes, lane->columns };
    const uint32_t names[4] = { BUSTER_CHECK_EMIT_SQUEEZE_OUTPUT, BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH, BUSTER_CHECK_EMIT_SQUEEZE_BYTES, BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH };
    const uint32_t count = arenas && lane->columns != lane->scratch ? 4 : 3;
    uint64_t capacities[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; arenas && i < count; i += 1)
    {
        capacities[i] = each[i]->capacity;
        const bool named = (lanes->squeeze_arenas & names[i]) != 0;
        each[i]->capacity = named && lanes->squeeze_room < capacities[i] - each[i]->length ? each[i]->length + lanes->squeeze_room : capacities[i];
    }
    lane->ready = lane->prepared ? lane->ready :
                                   arenas && check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->columns);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
    for (uint32_t i = 0; arenas && i < count; i += 1)
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

// Lane 0, alone in the flow: a round of the emission `run` on `lanes` of the flow's lanes (at least 2), after which it
// goes on alone. Its first round learns how the session's lanes started (lane_flow_lanes): false if they didn't, for the
// bodies to go on one lane.
static bool check_program_emit_round(LaneFlow* flow, CheckEmitRun* run, uint32_t lanes)
{
    const bool started = lane_flow_lanes(flow) > 1;
    if (started)
    {
        lane_flow_round_begin(flow, BUSTER_CHECK_EMIT_ROUND_BODIES, run, lanes);
        check_program_emit_lane(run);
        lane_flow_round_end(flow);
    }
    return started;
}

// The flow's other lanes: each round's bodies' parts on its lanes, until lane 0 ends the flow.
static void check_program_emit_follow(LaneFlow* flow)
{
    void* run = NULL;
    for (uint32_t round = lane_flow_next(flow, &run); round != 0; round = lane_flow_next(flow, &run))
    {
        if (run != NULL)
        {
            check_program_emit_lane(run);
        }
        lane_widen();
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

// Takes the next part of `share`'s open epoch: its place in the epoch's order into *taken and true, or false when the
// epoch has none left or none is open. A failed exchange loads the ticket's latest value, another lane's take or a new
// epoch's, which the loop reads against the limit again.
static bool check_program_share_take(CheckEmitShare* share, uint64_t* taken)
{
    uint64_t ticket = __atomic_load_n(&share->ticket, __ATOMIC_ACQUIRE);
    bool open = true;
    bool took = false;
    while (open && !took)
    {
        const uint64_t limit = __atomic_load_n(&share->limit, __ATOMIC_ACQUIRE);
        open = (limit >> 32) == (ticket >> 32) && (uint32_t)ticket < (uint32_t)limit;
        took = open && __atomic_compare_exchange_n(&share->ticket, &ticket, ticket + 1, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    }
    *taken = (uint32_t)ticket;
    return took;
}

// The part `taken` of `share`'s epoch, on its run's lane state `state` (a writer and arenas made at the state's first
// part, as a lane's: check_program_emit_lane_prepare), then counted done; the last part done publishes the epoch
// complete. Every access to the run, including its part count, precedes this part's acknowledgment: another part may
// publish complete as soon as this one increments done, letting the owner reuse the run and its arenas.
static void check_program_share_run(CheckEmitShare* share, uint64_t taken, uint32_t state)
{
    CheckEmitRun* run = share->run;
    CheckEmitLane* lane = run->lane_states + state;
    CheckEmitPart* part = run->parts + share->order[taken];
    lane->ready = lane->prepared ? lane->ready : check_program_emit_lane_prepare(run, lane, state);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
    const uint32_t parts = run->part_count;
    if (__atomic_add_fetch(&share->done, 1, __ATOMIC_ACQ_REL) == parts)
    {
        lane_publish(&share->complete, 2);
    }
}

// The bodies' parts shared on the units' board (#777, CheckProgramLanes.board): cut for `active` lanes as lanes of its
// own would cut them (parts_by_cost), opened as a new epoch of lane `index`'s share, taken by this lane (its run's first
// state, with the caller's arenas) and by the board's lanes that take no units, and waited for.
static void check_program_emit_shared(CheckEmitRun* run, CheckEmitBoard* board, uint32_t index, uint32_t active, uint32_t pause_microseconds)
{
    const uint64_t parts = parts_by_cost(run->costs, run->body_count, active, run->boundaries, run->orders[0]);
    for (uint64_t p = 0; p < parts; p += 1)
    {
        run->parts[p].lower.first = (uint32_t)run->boundaries[p];
        run->parts[p].lower.end = (uint32_t)run->boundaries[p + 1];
    }
    run->part_count = (uint32_t)parts;
    CheckEmitShare* share = board->shares + index;
    const uint64_t epoch = (__atomic_load_n(&share->ticket, __ATOMIC_RELAXED) >> 32) + 1;
    share->run = run;
    share->order = run->orders[0];
    __atomic_store_n(&share->done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&share->complete, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&share->limit, epoch << 32 | parts, __ATOMIC_RELEASE);
    __atomic_store_n(&share->ticket, epoch << 32, __ATOMIC_RELEASE);
    lane_advance(&board->generation);
    uint64_t taken = 0;
    while (check_program_share_take(share, &taken))
    {
        check_program_share_run(share, taken, 0);
        if (pause_microseconds != 0)
        {
            const struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)pause_microseconds * 1000 };
            nanosleep(&pause, NULL);
        }
    }
    // There is at least one part (the caller has bodies and active > 1). Acquire its last publisher even if done
    // already counts every part: it may still be about to publish complete, which must not reach the next epoch.
    lane_wait(&share->complete, 0);
}

// A lane of the units' board with no unit left to take (#777): takes the parts that the other lanes' emissions share,
// any lane's, until no lane takes units any more and no share has a part left. Its state in each emission is its place
// plus one (its arenas CheckProgramLanes.arenas' of the sharing lane, which that lane releases after its unit); a lane
// whose state would be past an emission's room takes none.
static void check_program_emit_help(CheckEmitBoard* board, uint32_t index)
{
    const uint32_t state = index + 1;
    bool helping = state < BUSTER_CHECK_EMIT_MAX_LANES;
    while (helping)
    {
        const uint32_t generation = __atomic_load_n(&board->generation, __ATOMIC_ACQUIRE) & ~1u;
        bool took = false;
        for (uint32_t i = 1; i <= board->lanes; i += 1)
        {
            CheckEmitShare* share = board->shares + (index + i) % board->lanes;
            uint64_t taken = 0;
            while (check_program_share_take(share, &taken))
            {
                check_program_share_run(share, taken, state);
                __atomic_add_fetch(&board->others, 1, __ATOMIC_RELAXED);
                took = true;
            }
        }
        // A share that opens, and a lane that stops taking units, advance the word after this lane read it. The wait is
        // for another lane's front end, a unit's preprocessing to its selection: long enough to sleep at once.
        helping = took || __atomic_load_n(&board->taking, __ATOMIC_ACQUIRE) != 0;
        if (!took && helping)
        {
            lane_sleep(&board->generation, generation);
        }
    }
}

// The asm templates' names' symbols (CheckProgramAsms.names), into `symbols`, by name: a label's a local symbol of its
// own, which the merge defines where its template's code puts it; another name's the program's symbol of that link name
// (its asm label or its identifier: a name the emission makes is no template's to know), found in one pass over the
// program's symbols; or else an undefined global symbol of its own, as GNU as and clang make one for a name the unit
// doesn't define. The symbols of its own are numbered from `first` in the names' order, `*own` of them, and `externals`
// (with binding, or 0) gets each undefined one's spelling. A label with one of the program's symbols' names is refused
// (`*reason`), as GNU as and clang refuse a symbol defined twice. False for that, or an arena without room. The pass is
// scalar: a unit's templates have few names (main.c's units 6), and it looks up only symbols' names of their lengths.
static bool check_program_emit_asm_symbols(const CheckProgram* program, const Parser* parser, const InternTable* identifiers, const String8* symbol_names,
                                           const CheckProgramAsms* asms, SymbolId first, SymbolId* symbols, uint32_t* own, String8* externals,
                                           uint32_t* external_count, const char** reason, Arena* scratch)
{
    const CheckInput* input = program->input;
    const uint32_t count = asms->name_count;
    uint32_t slots = 2;
    while (slots < 2 * (uint64_t)count)
    {
        slots *= 2;
    }
    const uint64_t mark = scratch->length;
    uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    bool valid = table != 0;
    // Bit i: a name of i bytes (63 for 63 and more), as a symbol's must be to be looked up.
    uint64_t lengths = 0;
    if (valid)
    {
        memset(table, 0, (uint64_t)slots * sizeof(uint32_t));
        for (uint32_t n = 0; n < count; n += 1)
        {
            const String8 spelling = asms->names[n].spelling;
            uint32_t slot = (uint32_t)link_hash((const uint8_t*)spelling.pointer, (uint32_t)spelling.length) & (slots - 1);
            while (table[slot] != 0)
            {
                slot = (slot + 1) & (slots - 1);
            }
            table[slot] = n + 1;
            symbols[n] = 0;
            lengths |= UINT64_C(1) << (spelling.length < 63 ? spelling.length : 63);
        }
    }
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        const String8 renamed = symbol_names[s + 1];
        // The names the source gives (check_program_emit_given_name's): not a literal's, a compound literal's or a
        // block-scope static's, which the emission makes.
        const bool given = declaration != BUSTER_CHECK_NONE && !input->compound_objects[declaration] &&
                           !(renamed.length == 0 && input->declaration_bodies[declaration] != BUSTER_CHECK_NONE && input->linkage[declaration] == 0);
        const uint32_t identifier = given ? parser->tokens.names[declaration] : 0;
        const String8 spelling = !given ? (String8){ 0 } : renamed.length != 0 ? renamed :
                                 (String8){ .pointer = (char*)intern_bytes(identifiers, identifier), .length = intern_length(identifiers, identifier) };
        if (given && (lengths >> (spelling.length < 63 ? spelling.length : 63) & 1) != 0)
        {
            uint32_t slot = (uint32_t)link_hash((const uint8_t*)spelling.pointer, (uint32_t)spelling.length) & (slots - 1);
            while (table[slot] != 0 && (asms->names[table[slot] - 1].spelling.length != spelling.length ||
                                        memcmp(asms->names[table[slot] - 1].spelling.pointer, spelling.pointer, spelling.length) != 0))
            {
                slot = (slot + 1) & (slots - 1);
            }
            const uint32_t n = table[slot] != 0 ? table[slot] - 1 : BUSTER_CHECK_NONE;
            if (n != BUSTER_CHECK_NONE && asms->names[n].label)
            {
                *reason = "inline assembly labels named as the unit's symbols";
                valid = false;
            }
            else if (n != BUSTER_CHECK_NONE)
            {
                symbols[n] = s + 1;
            }
        }
    }
    for (uint32_t n = 0; valid && n < count; n += 1)
    {
        if (asms->names[n].label || symbols[n] == 0)
        {
            symbols[n] = first + *own;
            *own += 1;
            if (!asms->names[n].label && externals != 0)
            {
                externals[*external_count] = asms->names[n].spelling;
                *external_count += 1;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The instructions of `code` that have references (x86_encode's marked ones): its references' distinct instructions. A
// template's may have two, a displacement's and an immediate's; Tier 0's have one each.
static uint64_t check_program_emit_marked(const Tier0Code* code)
{
    uint64_t marked = 0;
    for (uint32_t i = 0; i < code->reference_count; i += 1)
    {
        marked += i == 0 || code->references[i].instruction != code->references[i - 1].instruction;
    }
    return marked;
}

// The objects the unit defines, a byte per object in `defined` (1 where it does), and how Tier 0 reaches each of the
// program's symbols, `access` 1 to symbol_count (Tier0Function.symbol_access): a symbol this unit doesn't define may be a
// shared library's, which the direct path, without copy relocations, reaches only through its GOT slot (an executable's
// writer relaxes the load where the symbol is in the image), and a thread-local is the image's by local-exec, or
// another's by initial-exec. The unit defines its bodies' functions, the objects it defines or tentatively defines (as
// check_data_prepare selects them), its aliases and ifuncs (#147), its literals and whatever has no external linkage
// but a weakref's own symbol, a weak reference to another's name (check_program_alias_external). The emission's column
// (check_program_emit_lanes) and check_program_access's share it. The objects it defines are found among the tokens
// that define or tentatively define, few of a unit's (#592), 64 at a time (the scalar path, `scalar`, takes every token).
static void check_program_object_access(const CheckProgram* program, uint8_t* access, uint8_t* defined, bool scalar)
{
    (void)scalar;
    const CheckInput* input = program->input;
    // Only symbols' objects are consumed below or by the linker's external-name collection.
    for (uint32_t s = 0; s < program->symbol_count; s += 1)
    {
        defined[program->symbol_objects[s]] = 0;
    }
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        defined[program->bodies[b].object] = 1;
    }
    for (uint32_t first = 0; first < input->token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, input->token_count) :
                                        check_input_scan_nonzero(input->static_objects, first, input->token_count);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            const uint32_t object = input->objects[token];
            if (input->static_objects[token] && input->declarations[token] == token && object < program->object_count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
                (input->defined[token] || input->tentative[token]))
            {
                defined[object] = 1;
            }
        }
    }
    // GNU's aliases and ifuncs are definitions of the unit's (#147), whose symbols the emission defines at their bases.
    for (uint32_t k = 0; k < program->alias_count; k += 1)
    {
        defined[program->aliases[k].object] |= program->aliases[k].kind != CHECK_PROGRAM_WEAKREF;
    }
    for (uint32_t s = 0; s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        const bool literal = declaration == BUSTER_CHECK_NONE || program->input->compound_objects[declaration];
        // A weakref's own symbol (one without a target in the unit) is a weak reference to another's name, and so is a
        // weakref's target of internal linkage that the unit doesn't define and refers to through weakrefs alone.
        const bool internal = !literal && !defined[object] && program->input->linkage[declaration] != 2;
        const bool external = internal && program->alias_count != 0 &&
                              (check_program_alias_external(program, object) || check_program_alias_weak_target(program, object));
        const bool own = literal || defined[object] || (program->input->linkage[declaration] != 2 && !external);
        const bool thread_local = !literal && program->input->thread_local[declaration];
        access[s + 1] = thread_local ? (own ? BUSTER_TIER0_ACCESS_LOCAL_EXEC : BUSTER_TIER0_ACCESS_INITIAL_EXEC) : own ? BUSTER_TIER0_ACCESS_DIRECT : BUSTER_TIER0_ACCESS_GOT;
    }
}

// How Tier 0 reaches each symbol of a lowered program, as check_lower_finish numbered them (Tier0Function.symbol_access):
// one column, from `output`, for every body that `code`'s emission lowered of `program`, and `*defined_objects`, from
// `scratch` (check_program_object_access). It has an entry for every symbol of the emission's link (CheckProgramCode.link):
// the jump tables' and their labels' too, after the runtime functions', the unit's own and direct (Tier 0 compiles a
// symbol past its column as direct, but tier0_validate refuses one). The helpers are the unit's, and the C runtime's
// errno function isn't; libgcc's functions are linked into the image.
// Returns whether there was room. benchmark-backend-source (tier0_source_benchmark.c) compiles the lowered functions
// again with it: the emission's own column, made before its lanes, has the runtime functions' provisional symbols
// (check_lower_runtime), and goes with its scratch.
static bool check_program_access(CheckProgramCode* code, const CheckProgram* program, Arena* output, Arena* scratch, uint8_t** defined_objects)
{
    CheckProgramLowered* lowered = &code->lowered;
    const uint32_t count = program->body_count;
    const uint32_t numbered = program->symbol_count + lowered->helpers.count + lowered->initializers.count + lowered->runtime_symbol_count + 1;
    const uint32_t access_count = code->link.symbols.count > numbered ? code->link.symbols.count : numbered;
    uint8_t* access = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, access_count);
    uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1);
    const bool valid = access != 0 && defined != 0;
    *defined_objects = defined;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined, false);
        for (uint32_t r = 0; r < lowered->runtime_symbol_count; r += 1)
        {
            const CheckLowerRuntime runtime = (CheckLowerRuntime)lowered->runtime_order[r];
            const uint32_t symbol = lowered->runtime_symbols[runtime];
            // The C library's and libm's functions are a shared library's, which the GOT reaches.
            access[symbol] = runtime == CHECK_LOWER_RUNTIME_ERRNO || runtime == CHECK_LOWER_RUNTIME_MEMCPY ||
                                     (runtime >= CHECK_LOWER_RUNTIME_FMINF && runtime <= CHECK_LOWER_RUNTIME_FMAX) || runtime >= BUSTER_CHECK_LOWER_LIBM_FIRST
                                 ? BUSTER_TIER0_ACCESS_GOT
                                 : BUSTER_TIER0_ACCESS_DIRECT;
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
// #145's c-testsuite harness's, check of Tier 0's output), or the emission fails with CHECK_PROGRAM_BACKEND; without
// it, only their branch columns are checked (branch_sizing_validate_branches), which sizing needs inside each
// function, since Tier 0's records are valid by construction and x86_validate of all of them made branch sizing take
// 14 times the encoder's time on sqlite3.c (#532). With
// `rows`, each source body's raw source rows too (c_check_debug.c), from `output`. With `lanes`, on up to lanes->lanes
// lanes (above): one, and one more per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens beyond twice the largest
// body's, but with lanes->every_lane as many as there are parts for; without, on the calling thread alone. With
// `stage_clock` (the driver's --time-stages), each step's time is charged to its stage (c_check_clock.c).
// The unit's file-scope assembly (#619), as clang emits a module's: its statements' templates in source order, a line
// each, before the functions, assembled by assembler.c into sections of its own (asm_assemble). Of what it makes, the
// emission takes the sections' bytes and alignments into atoms of their classes before the functions' (`file_asm`, their
// bytes in `bytes` and their descriptors' names in `output`), each in its section in -c's object
// (CheckProgramCode.asm_sections), as main.c's `.section`, `.p2align` and `.text` make an empty, page-aligned
// .text.buster_checker. Refused by name: a template the assembler doesn't take, one that defines or refers to a symbol
// (a label, an instruction's operand, CFI), which the unit's symbols don't join yet, and a section of a class C's
// definitions don't have (an init array's, a note). Returns false with `result`'s status and reason set.
typedef struct CheckProgramFileAsm CheckProgramFileAsm;
struct CheckProgramFileAsm
{
    uint8_t* classes;
    uint8_t* alignments;
    uint64_t* sizes;
    uint8_t** bytes;
    ObjectAtomSection* sections;
    uint32_t count;
};

static bool check_program_emit_file_asm(CheckProgramFileAsm* file_asm, CheckProgramCode* result, const CheckProgram* program, bool scalar, Arena* bytes,
                                        Arena* output, Arena* scratch)
{
    const CheckInput* input = program->input;
    *file_asm = (CheckProgramFileAsm){ 0 };
    uint64_t length = 0;
    uint32_t first = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < input->asm_count; i += 1)
    {
        const CheckInputAsm* statement = input->asm_statements + i;
        length += statement->body == BUSTER_CHECK_NONE ? (uint64_t)statement->template_text.length + 1 : 0;
        first = statement->body == BUSTER_CHECK_NONE && first == BUSTER_CHECK_NONE ? statement->token : first;
    }
    bool valid = true;
    if (first != BUSTER_CHECK_NONE)
    {
        const uint64_t mark = scratch->length;
        char* source = length < UINT32_MAX ? arena_push(scratch, length, 1) : 0;
        const X86MatchIndex* index = source != 0 ? check_asm_matches(scratch) : 0;
        valid = index != 0;
        uint64_t at = 0;
        for (uint32_t i = 0; valid && i < input->asm_count; i += 1)
        {
            const CheckInputAsm* statement = input->asm_statements + i;
            if (statement->body == BUSTER_CHECK_NONE)
            {
                memcpy(source + at, statement->template_text.bytes, statement->template_text.length);
                at += statement->template_text.length;
                source[at] = '\n';
                at += 1;
            }
        }
        const AsmResult assembled = valid ? asm_assemble((String8){ .pointer = source, .length = length }, index, (AsmOptions){ .vectorized = !scalar }, scratch) :
                                            (AsmResult){ .error = "" };
        const Link* link = &assembled.link;
        const char* refused = !valid ? NULL :
                              assembled.error != 0 ? "file-scope assembly the assembler doesn't take" :
                              link->symbols.count > 1 || link->fixups.count > 1 ? "file-scope assembly that defines or refers to a symbol" :
                              NULL;
        // A section of a class C's definitions have, of its type (or with bytes, PROGBITS), as the object writer takes it:
        // not an init array's (.init_array is one in a data class), a note's or a group's.
        for (AtomId atom = 1; valid && refused == NULL && atom < link->atoms.count; atom += 1)
        {
            const uint8_t class = link->atoms.class[atom];
            const uint32_t type = assembled.object_options.atom_sections[atom].type;
            const bool kind = class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_RODATA || class == BUSTER_LINK_CLASS_DATA ||
                              class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_BSS || class == BUSTER_LINK_CLASS_TDATA ||
                              class == BUSTER_LINK_CLASS_TBSS;
            refused = kind && (type == object_class_sections[class].type || (!link_class_is_zero_fill(class) && type == SHT_PROGBITS)) ? NULL :
                      "file-scope assembly's section of this kind";
        }
        const uint32_t count = valid && refused == NULL && link->atoms.count > 1 ? link->atoms.count - 1 : 0;
        file_asm->classes = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count) : 0;
        file_asm->alignments = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count) : 0;
        file_asm->sizes = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, count) : 0;
        file_asm->bytes = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t*, count) : 0;
        file_asm->sections = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, ObjectAtomSection, count) : 0;
        valid = valid && (count == 0 || (file_asm->classes != 0 && file_asm->alignments != 0 && file_asm->sizes != 0 && file_asm->bytes != 0 &&
                                         file_asm->sections != 0));
        for (uint32_t a = 0; valid && a < count; a += 1)
        {
            const AtomId atom = a + 1;
            const uint64_t size = link->atoms.size[atom];
            const bool zero = link_class_is_zero_fill(link->atoms.class[atom]);
            const ObjectAtomSection section = assembled.object_options.atom_sections[atom];
            uint8_t* copy = !zero && size != 0 ? arena_push(bytes, size, 16) : 0;
            char* name = section.name.length != 0 ? arena_push(output, section.name.length, 1) : 0;
            valid = (zero || size == 0 || copy != 0) && (section.name.length == 0 || name != 0);
            if (valid)
            {
                if (copy != 0)
                {
                    memcpy(copy, link->atoms.bytes[atom], size);
                }
                if (name != 0)
                {
                    memcpy(name, section.name.pointer, section.name.length);
                }
                file_asm->classes[a] = link->atoms.class[atom];
                file_asm->alignments[a] = link->atoms.alignment_log2[atom];
                file_asm->sizes[a] = size;
                file_asm->bytes[a] = copy;
                file_asm->sections[a] = section;
                file_asm->sections[a].name = (String8){ .pointer = name, .length = section.name.length };
            }
        }
        file_asm->count = valid ? count : 0;
        if (!valid || refused != NULL)
        {
            result->status = refused != NULL ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
            result->reason = refused;
            result->token = first;
            valid = false;
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

static bool check_program_emit_timed(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                                     const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                                     CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify, CheckProgramLanes* lanes,
                                     CheckClock* stage_clock)
{
    (void)scalar;

    *result = (CheckProgramCode){ .token = BUSTER_CHECK_NONE };
    const bool timing = lanes != NULL && lanes->timing;
    const uint64_t started = timing ? check_program_lanes_now() : 0;
    const uint64_t mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = program->body_count;
    bool valid = output != bytes && output != scratch && bytes != scratch && program->status == CHECK_PROGRAM_OK;
    result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && count != 0 && !tier0_initialized)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    if (valid && count != 0 && unwind && !unwind_ready)
    {
        valid = unwind_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    check_clock_mark(stage_clock, CHECK_STAGE_BACKEND_SETUP);
    CheckLowerShared shared;
    const bool lowering = valid;
    if (valid)
    {
        valid = check_lower_prepare(&shared, &result->lowered, program, BUSTER_SCALAR_PATH(scalar), rows != NULL, output, scratch);
    }
    check_clock_mark(stage_clock, CHECK_STAGE_LOWER);
    // How Tier 0 reaches each symbol (Tier0Function.symbol_access), one column for every body: the program's symbols' as
    // check_program_object_access gives them. The helpers are the unit's, and the C runtime's errno function isn't;
    // libgcc's functions are linked into the image. The runtime's functions have their provisional symbols while the
    // bodies compile (check_lower_runtime), each its own, and the jump tables theirs after them
    // (CheckLowerShared.provisional_tables), the unit's own: direct.
    const uint32_t access_count = valid ? shared.provisional_tables + result->lowered.table_count : 0;
    uint8_t* access = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, access_count) : 0;
    uint8_t* defined = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    valid = valid && access != 0 && defined != 0;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined, BUSTER_SCALAR_PATH(scalar));
        access[shared.provisional_runtimes + CHECK_LOWER_RUNTIME_ERRNO] = BUSTER_TIER0_ACCESS_GOT;
        access[shared.provisional_runtimes + CHECK_LOWER_RUNTIME_MEMCPY] = BUSTER_TIER0_ACCESS_GOT;
    }
    // Lane 0's writer's columns (CheckEmitRun.columns), in an arena of base pages reserved for the room they take
    // (check_lower_writer_bytes): the lanes' first arenas' scratch, kept while it has the room and otherwise reserved
    // again, or without lanes an arena of this emission's own; none without bodies, whose writer never begins. Where that
    // address space can't be reserved (RLIMIT_AS, or overcommit refused), they go in the caller's scratch, as before
    // #531, if it has their room: the calling thread runs the bodies when the lanes can't, and then needs no address space
    // past the caller's arenas.
    Arena own_columns = { 0 };
    Arena* reserved = lanes != NULL ? &lanes->arenas[0].scratch : &own_columns;
    const uint64_t column_bytes = valid && count != 0 ? check_lower_writer_bytes(&shared, rows != NULL) : 0;
    if (column_bytes > reserved->capacity)
    {
        arena_release(reserved);
        // In base pages throughout: `small` past the capacity.
        *reserved = arena_reserve_small(column_bytes, BUSTER_ARENA_MAX_CAPACITY);
    }
    arena_pop_to(reserved, 0);
    Arena* columns = column_bytes != 0 && reserved->base != 0 ? reserved : scratch;
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
        .scalar = BUSTER_SCALAR_PATH(scalar),
        .verify = verify,
        .unwind = unwind,
        .sources = rows != NULL,
        .output = output,
        .scratch = scratch,
        .bytes = bytes,
        .columns = columns,
    };
    run.codes = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Code, count) : 0;
    run.bodies = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitBody, count) : 0;
    run.frames = valid && frames != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, UnwindFunction, frames) : 0;
    run.costs = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    // --tier1's outcomes, and its tables, once: the cost model's (x86_zen5.c), which its selection reads.
    const bool tier1 = valid && count != 0 && lanes != NULL && lanes->tier1;
    run.tier1_outcomes = tier1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    run.tier1_counts = tier1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTier1Counts, count) : 0;
    if (run.tier1_outcomes != 0)
    {
        // A body a failure before it left unreached has none (an outcome past the stages).
        memset(run.tier1_outcomes, 0xff, (uint64_t)count * sizeof(uint32_t));
    }
    if (tier1 && run.tier1_outcomes != 0 && !check_tier1_ready)
    {
        check_tier1_ready = x86_zen5_initialize();
        result->status = check_tier1_ready ? result->status : CHECK_PROGRAM_BACKEND;
    }
    valid = valid && (count == 0 || (run.codes != 0 && run.bodies != 0)) && (frames == 0 || run.frames != 0) && run.costs != 0 &&
            (!tier1 || (run.tier1_outcomes != 0 && run.tier1_counts != 0 && check_tier1_ready));
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
    // more than the bodies, which are at least as many as the parts. On the units' board (#777), only while one of its
    // lanes takes no units: an emission that starts while every lane still takes units runs alone, its bodies one part.
    CheckEmitBoard* board = lanes != NULL && lanes->board != NULL && lanes->board_lane < lanes->board->lanes ? lanes->board : NULL;
    const bool sharing = board == NULL || __atomic_load_n(&board->taking, __ATOMIC_ACQUIRE) < board->lanes;
    const uint32_t most = lanes != NULL && lanes->lanes > 1 && sharing ? (lanes->lanes < BUSTER_CHECK_EMIT_MAX_LANES ? lanes->lanes : BUSTER_CHECK_EMIT_MAX_LANES) : 1;
    const uint64_t paid = lanes != NULL && lanes->every_lane ? UINT64_MAX : 1 + (total > 2 * largest ? total - 2 * largest : 0) / BUSTER_CHECK_EMIT_LANE_TOKENS;
    const uint32_t bodies = count != 0 ? count : 1;
    uint32_t active = most < bodies ? most : bodies;
    active = paid < active ? (uint32_t)paid : active;
    // In a command's flow (#905), no more than its session asks for: by default one lane per core, which a lazy session's
    // lane 1 has been counting meanwhile (lane_session_asked).
    if (active > 1 && lanes->flow != NULL)
    {
        const uint32_t asked = lane_session_asked();
        active = asked < active ? asked : active;
    }
    WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
    if (valid && count != 0 && lanes != NULL && lanes->budget != NULL)
    {
        active = worker_budget_acquire(lanes->budget, active, &lease);
    }
    const uint64_t part_room = active > 1 ? (uint64_t)BUSTER_LANE_PARTS_PER_LANE * active : 1;
    // On the board, a state for the sharing lane and one for each lane that may take its parts, its place plus one.
    const uint32_t states = board != NULL && active > 1 ? (board->lanes < BUSTER_CHECK_EMIT_MAX_LANES ? board->lanes + 1 : BUSTER_CHECK_EMIT_MAX_LANES) : active;
    run.parts = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitPart, part_room) : 0;
    run.lane_states = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitLane, states) : 0;
    run.boundaries = valid && active > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, part_room + 1) : 0;
    // Each lane's order of the parts, or on the board the one order every lane takes them in.
    for (uint32_t lane = 0; valid && active > 1 && lane < (board != NULL ? 1 : active); lane += 1)
    {
        run.orders[lane] = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, part_room + 1);
        valid = run.orders[lane] != 0;
    }
    valid = valid && run.parts != 0 && run.lane_states != 0 && (active == 1 || run.boundaries != 0);
    check_clock_mark(stage_clock, CHECK_STAGE_EMIT_SETUP);
    CheckProgramAgain again = CHECK_PROGRAM_AGAIN_NONE;
    if (lanes != NULL)
    {
        lanes->squeezed = 0;
    }
    const uint64_t lanes_begin = timing ? check_program_lanes_now() : 0;
    if (valid && count != 0)
    {
        memset(run.lane_states, 0, (uint64_t)states * sizeof(CheckEmitLane));
        // The lanes' run, or the calling thread's alone; lanes that couldn't start, or a part that ran out of room, go
        // again on the calling thread alone, from the arenas as they were (each body sets what it reads of the shared
        // tables before it reads it, and puts local_map's entries back).
        const uint64_t output_before = output->length;
        const uint64_t bytes_before = bytes->length;
        const uint64_t scratch_before = scratch->length;
        const uint64_t columns_before = columns->length;
        if (active > 1)
        {
            if (board != NULL)
            {
                check_program_emit_shared(&run, board, lanes->board_lane, active, lanes->board_pause_microseconds);
            }
            else if (lanes->flow != NULL)
            {
                // The flow's lanes (#905), which a lazy session's lane 1 has been starting meanwhile: a round of them, or
                // if they didn't start, the bodies again on one lane.
                again = check_program_emit_round(lanes->flow, &run, active) ? CHECK_PROGRAM_AGAIN_NONE : CHECK_PROGRAM_AGAIN_START;
            }
            else
            {
                again = lane_run(active, check_program_emit_lane, &run, lanes->cpus) ? CHECK_PROGRAM_AGAIN_NONE : CHECK_PROGRAM_AGAIN_START;
            }
            valid = worker_budget_release(&lease);
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
            check_clock_mark(stage_clock, CHECK_STAGE_LANES);
        }
        else
        {
            valid = worker_budget_release(&lease);
        }
        if (valid && (active == 1 || again != CHECK_PROGRAM_AGAIN_NONE))
        {
            arena_pop_to(output, output_before);
            arena_pop_to(bytes, bytes_before);
            arena_pop_to(scratch, scratch_before);
            arena_pop_to(columns, columns_before);
            memset(run.lane_states, 0, sizeof(CheckEmitLane));
            run.stage_clock = stage_clock;
            check_program_emit_alone(&run);
        }
    }
    // Also return a lease when an active-lane allocation failed before dispatch. A returned lease is a no-op.
    valid = worker_budget_release(&lease) && valid;
    if (lanes != NULL && lanes->budget != NULL && lanes->budget->return_failed)
    {
        result->status = CHECK_PROGRAM_BACKEND;
        result->reason = "could not return GNU make worker slots";
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
    for (uint32_t b = 0; valid && run.tier1_outcomes != 0 && b < count; b += 1)
    {
        const uint32_t outcome = run.tier1_outcomes[b];
        const uint32_t stage = outcome & 0xff;
        lanes->tier1_report.functions += stage < CHECK_TIER1_STAGE_COUNT;
        lanes->tier1_report.stages[stage < CHECK_TIER1_STAGE_COUNT ? stage : 0] += stage < CHECK_TIER1_STAGE_COUNT;
        lanes->tier1_report.operations[(outcome >> 8) % BUSTER_CHECK_TIER1_OPERATIONS] += stage == CHECK_TIER1_SELECTION;
        lanes->tier1_report.emission[(outcome >> 8) % BUSTER_CHECK_TIER1_OPERATIONS] += stage == CHECK_TIER1_EMISSION;
        const CheckTier1Counts* counts = &run.tier1_counts[b];
        const bool compiled = stage == CHECK_TIER1_COMPILED;
        lanes->tier1_report.records += compiled ? counts->records : 0;
        lanes->tier1_report.spill_stores += compiled ? counts->spill_stores : 0;
        lanes->tier1_report.spill_loads += compiled ? counts->spill_loads : 0;
        lanes->tier1_report.call_stores += compiled ? counts->call_stores : 0;
        lanes->tier1_report.call_loads += compiled ? counts->call_loads : 0;
        lanes->tier1_report.rematerializations += compiled ? counts->rematerializations : 0;
        lanes->tier1_report.dropped_definitions += compiled ? counts->dropped_definitions : 0;
        lanes->tier1_report.edge_moves += compiled ? counts->edge_moves : 0;
        lanes->tier1_report.spill_slots += compiled ? counts->spill_slots : 0;
        lanes->tier1_report.frame_bytes += compiled ? counts->frame_bytes : 0;
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
    check_clock_mark(stage_clock, CHECK_STAGE_LOWER);
    CheckProgramFileAsm file_asm = { 0 };
    valid = valid && check_program_emit_file_asm(&file_asm, result, program, scalar, bytes, output, scratch);
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    String8* symbol_names = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + 1) : 0;
    // Each symbol's weak binding, and its constructor's and destructor's priorities (#147), two a symbol, each plus one (0
    // for none), with the declaration whose attribute gave it: the latest declaration's first, as clang takes it.
    uint8_t* weak = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    uint32_t* structors = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * ((uint64_t)program->symbol_count + 1)) : 0;
    uint32_t* structor_tokens = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * ((uint64_t)program->symbol_count + 1)) : 0;
    // Each symbol's alias, ifunc or weakref (#147), its entry plus one: an alias's or ifunc's own symbol, and a weakref's
    // own one (a weak reference to its name); none in a unit without them. And whether a retain attribute (#147) keeps
    // its definition's section.
    uint32_t* symbol_aliases = valid && program->alias_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->symbol_count + 1) : 0;
    uint8_t* retained = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    bool retaining = false;
    valid = valid && weak != 0 && structors != 0 && structor_tokens != 0 && (program->alias_count == 0 || symbol_aliases != 0) && retained != 0;
    result->function_atoms = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, count) : 0;
    // The declaring tokens, for the attributes' passes (one vector pass: cheaper than holding them through the
    // bodies' compiles), pushed last and given back after them.
    const uint64_t attributes_mark = scratch->length;
    uint32_t* declared = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->input->token_count + 16) : 0;
    valid = valid && alignments != 0 && symbol_names != 0 && declared != 0 && (count == 0 || result->function_atoms != 0);
    if (valid)
    {
        memset(alignments, 0, (uint64_t)program->symbol_count + 1);
        memset(symbol_names, 0, ((uint64_t)program->symbol_count + 1) * sizeof(String8));
        memset(weak, 0, (uint64_t)program->symbol_count + 1);
        memset(retained, 0, (uint64_t)program->symbol_count + 1);
        memset(structors, 0, 2 * ((uint64_t)program->symbol_count + 1) * sizeof(uint32_t));
        // Find attribute pointers first, eight at a time, then keep the declaring tokens. This avoids reading every
        // declaration's attributes and keeps parameters' retain obligations as well as static declarations'.
        uint32_t attributed = 0;
        for (uint32_t first = 0; first < program->input->token_count; first += 8)
        {
            const uint32_t lanes = check_input_asm_attributed(program->input, first, program->input->token_count, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                if (program->input->declarations[token] == token && program->input->attributes[token] != NULL)
                {
                    declared[attributed++] = token;
                }
            }
        }
        // The first emitted variable under active #pragma clang section object settings, or BUSTER_CHECK_NONE (#619).
        // Static objects are conservative candidates under any active object setting; section-kind selection belongs
        // with actual section emission, so the refusal must not claim that this object would use the named section. A
        // function's sections, a text setting's and a section attribute's, are check_program_sections'.
        const CheckInputSections* sections = program->input->sections;
        uint32_t placed = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; sections != NULL && placed == BUSTER_CHECK_NONE && i < sections->definition_count; i += 1)
        {
            const uint32_t token = sections->definitions[i];
            const uint32_t object = token < program->input->token_count ? program->input->objects[token] : 0;
            placed = object != 0 && object < program->object_count && program->object_symbols[object] != 0 ? token : placed;
        }
        for (uint32_t d = 0; valid && d < attributed && declared[d] <= placed; d += 1)
        {
            const uint32_t token = declared[d];
            const CheckInputAttributes* attributes = program->input->attributes[token];
            const uint32_t object = program->input->objects[token];
            if (object != 0 && object < program->object_count)
            {
                // The object's own symbol: a referenced weakref's is its target's (#147), which its attributes don't touch.
                const SymbolId taken = program->object_symbols[object];
                const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
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
                    // A weak declaration's symbol is weak: the reader ignores one after the definition and refuses one
                    // without external linkage. A constructor's or destructor's priority is its argument, 65535 without
                    // one, past which LLVM takes 65535 (AsmPrinter's preprocessXXStructorList).
                    weak[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_WEAK;
                    const uint32_t structor = (kind == CHECK_ATTRIBUTE_DESTRUCTOR ? 1u : 0u) + 2 * symbol;
                    if (symbol != 0 && !attribute->ignored && (kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR) &&
                        program->input->declaration_kinds[token] == C_BINDING_FUNCTION && (structors[structor] == 0 || structor_tokens[structor] < token))
                    {
                        const uint64_t priority = attribute->argument_count != 0 && attribute->resolved ? (uint64_t)attribute->value : 65535;
                        structors[structor] = (uint32_t)(priority < 65535 ? priority : 65535) + 1;
                        structor_tokens[structor] = token;
                    }
                    // A retain attribute keeps an emitted definition's section (the reader ignores one after the
                    // definition), and makes it one of its own (check_program_sections).
                    retained[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_RETAIN;
                    retaining |= retained[symbol];
                    if (!attribute->ignored && symbol != 0 && kind == CHECK_ATTRIBUTE_COMMON)
                    {
                        // The typed source contract retains this obligation. Until common symbols (SHN_COMMON), which
                        // neither the link representation nor the object writer has, are published, refusing code
                        // emission is required. A dropped definition has no object-format obligation of its own.
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                        result->reason = "this attribute (common)";
                        result->token = attribute->name_token;
                        valid = false;
                    }
                }
            }
        }
        // GNU's alias, ifunc and weakref attributes (#147): each entry's own symbol, and its name where it is a weakref's,
        // a weak reference to its target's name; a weakref's target's undefined symbol is weak where the unit refers to it
        // through weakrefs alone (BUSTER_CHECK_PROGRAM_ALIAS_WEAK). An alias or ifunc clang's code generation refuses (the
        // driver reports its error before the emission: check_driver_aliases), or one the emission doesn't take, is
        // refused at its attribute, in source order.
        if (symbol_aliases != 0)
        {
            memset(symbol_aliases, 0, ((uint64_t)program->symbol_count + 1) * sizeof(uint32_t));
        }
        uint32_t refused = BUSTER_CHECK_NONE;
        for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
        {
            const CheckProgramAlias* alias = program->aliases + k;
            const SymbolId taken = program->object_symbols[alias->object];
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == alias->object ? taken : 0;
            const uint8_t state = program->alias_states[k];
            const bool failed = alias->error != CHECK_PROGRAM_ALIAS_OK || (state & BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED) != 0 ||
                                (alias->kind != CHECK_PROGRAM_WEAKREF && alias->base == 0);
            refused = failed && alias->attribute < refused ? alias->attribute : refused;
            if (symbol != 0)
            {
                symbol_aliases[symbol] = k + 1;
                symbol_names[symbol] = alias->kind == CHECK_PROGRAM_WEAKREF ? alias->target : symbol_names[symbol];
            }
            const SymbolId target = alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0 ? program->object_symbols[alias->target_object] : 0;
            weak[target] |= target != 0 && (state & BUSTER_CHECK_PROGRAM_ALIAS_WEAK) != 0 && !defined[alias->target_object];
        }
        for (uint32_t k = 0; valid && refused != BUSTER_CHECK_NONE && k < program->alias_count; k += 1)
        {
            const CheckProgramAlias* alias = program->aliases + k;
            if (alias->attribute == refused)
            {
                result->status = CHECK_PROGRAM_UNSUPPORTED;
                result->reason = alias->error == CHECK_PROGRAM_ALIAS_THREAD_LOCAL ? "an alias whose thread-local storage isn't its definition's" :
                                 alias->error == CHECK_PROGRAM_ALIAS_DEFINED ? "an alias variable that the unit also defines tentatively" :
                                 alias->kind == CHECK_PROGRAM_WEAKREF ? "a weakref without a target" :
                                                                         "an alias or ifunc that clang's code generation refuses";
                result->token = refused;
                valid = false;
            }
        }
        // A variable under active #pragma clang section object settings (#619) is refused, where no attribute's refusal
        // comes before it: the loop above stops at its token. Such a variable needn't have an attribute at all (a section
        // attribute of its own keeps it out), so the settings list them (CheckInputSections.definitions).
        if (valid && placed != BUSTER_CHECK_NONE)
        {
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "a definition under active '#pragma clang section' settings";
            result->token = placed;
            valid = false;
        }
    }
    arena_pop_to(scratch, attributes_mark);
    // The retained symbols (#147), for check_program_sections, in symbol order; none in the usual unit, which has no
    // retain attribute for the counting pass to look for.
    uint32_t retained_count = 0;
    for (uint32_t s = 1; valid && retaining && s <= program->symbol_count; s += 1)
    {
        retained_count += retained[s];
    }
    result->retained_symbols = valid && retained_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, retained_count) : 0;
    valid = valid && (retained_count == 0 || result->retained_symbols != 0);
    for (uint32_t s = 1; valid && retained_count != 0 && s <= program->symbol_count; s += 1)
    {
        if (retained[s])
        {
            result->retained_symbols[result->retained_count++] = s;
        }
    }
    // Symbols already carry the checker's selected object identities. Assign each overload's ABI name once, after
    // asm labels (which override mangling), before external binding, static-local naming and object publication.
    CheckMangleSource mangling = {
        .input = program->input,
        .parser = parser,
        .identifiers = identifiers,
        .types = program->types,
        .output = scratch,
        .scalar = scalar,
    };
    Scratch mangle_scratch = { 0 };
    for (uint32_t i = 0; valid && i < program->input->graph.overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = program->input->graph.overload_candidates + i;
        const SymbolId symbol = candidate->object != 0 && candidate->object < program->object_count ? program->object_symbols[candidate->object] : 0;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && symbol != 0 && symbol_names[symbol].length == 0)
        {
            if (mangle_scratch.arena == NULL)
            {
                mangle_scratch = scratch_begin(scratch);
            }
            mangling.internal = program->input->linkage[candidate->declaration] == 1;
            const CheckMangleStatus status = check_mangle_name(&mangling, candidate->type, parser->tokens.names[candidate->declaration],
                                                               mangle_scratch.arena, &symbol_names[symbol]);
            valid = status == CHECK_MANGLE_OK;
            if (!valid)
            {
                result->status = status == CHECK_MANGLE_RESOURCE ? CHECK_PROGRAM_RESOURCE :
                                 status == CHECK_MANGLE_INVALID ? CHECK_PROGRAM_INVALID : CHECK_PROGRAM_UNSUPPORTED;
                result->reason = "this overloadable signature's ABI name (local/anonymous tags, variable arrays or calling convention)";
                result->token = candidate->declaration;
            }
        }
    }
    if (mangle_scratch.arena != NULL)
    {
        scratch_end(mangle_scratch);
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
        result->reason = code->error == TIER0_ERROR_DEPTH ? "more values live at once than Tier 0's deep slots take" :
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
    const CheckProgramAsms* asms = &result->lowered.asms;
    String8* externals = binding != NULL && valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + result->lowered.runtime_symbol_count +
                                                                                                    asms->name_count + 1) : 0;
    uint32_t external_count = 0;
    valid = valid && (binding == NULL || externals != 0);
    for (uint32_t s = 0; valid && externals != 0 && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        // A weakref's own symbol (#147) refers to its name, which symbol_names holds, and a weakref's target referred to
        // through weakrefs alone is weak whatever its linkage.
        const bool weakref = symbol_aliases != 0 && symbol_aliases[s + 1] != 0 && program->aliases[symbol_aliases[s + 1] - 1].kind == CHECK_PROGRAM_WEAKREF;
        if (declaration != BUSTER_CHECK_NONE && !program->input->compound_objects[declaration] && !defined[object] &&
            (program->input->linkage[declaration] == 2 || weakref || weak[s + 1]))
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
    // The jump tables: an atom each, a fixup per entry, and their symbols and their labels'.
    uint64_t table_entries = 0;
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        table_entries += result->lowered.tables[t].count;
    }
    const uint64_t switch_symbols = valid ? (uint64_t)result->lowered.table_count + result->lowered.case_symbol_count : 0;
    // The asm templates' names' symbols (check_program_emit_asm_symbols), their own after the runtime's and the jump
    // tables' (CheckProgramLowered.case_symbol_base): its undefined globals among the names the unit refers to without
    // defining.
    const CheckLowerInitializers* pools = &result->lowered.initializers;
    const SymbolId asm_first = (SymbolId)(program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + 1 + switch_symbols);
    SymbolId* asm_symbols = valid && asms->name_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, asms->name_count) : 0;
    uint32_t asm_own = 0;
    valid = valid && (asms->name_count == 0 || asm_symbols != 0);
    if (valid && asms->name_count != 0)
    {
        const char* reason = NULL;
        valid = check_program_emit_asm_symbols(program, parser, identifiers, symbol_names, asms, asm_first, asm_symbols, &asm_own, externals, &external_count,
                                               &reason, scratch);
        result->status = valid ? result->status : reason != NULL ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
        result->reason = reason != NULL ? reason : result->reason;
    }
    const LinkCapacity runtime = binding != NULL && valid ? bind_capacity_needed(binding->runtime, externals, external_count, scratch, &name_bytes, &atom_bytes) :
                                                            (LinkCapacity){ 0 };
    // The defined functions' constructors' and destructors' entries, an atom of an init or fini array each (#147).
    uint32_t structor_count = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const SymbolId symbol = program->object_symbols[program->bodies[b].object];
        structor_count += (structors[2 * symbol] != 0) + (structors[2 * symbol + 1] != 0);
    }
    const uint64_t atom_count = (uint64_t)data_capacity.atoms + count + helpers->count + pools->count + runtime.atoms + (frames != 0 ? 1 : 0) +
                                (valid ? result->lowered.table_count : 0) + file_asm.count + structor_count;
    const uint64_t fixup_count = (uint64_t)data_capacity.fixups + references + runtime.fixups + frames + table_entries + structor_count;
    const uint64_t wide_count = (uint64_t)data_capacity.wide_addends + references + runtime.wide_addends;
    const uint64_t symbol_count = (uint64_t)program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + switch_symbols + asm_own + runtime.symbols;
    const uint64_t name_count = (uint64_t)program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + switch_symbols + asm_own + runtime.names;
    valid = valid && atom_count < UINT32_MAX && fixup_count < UINT32_MAX && wide_count < UINT32_MAX && symbol_count < UINT32_MAX &&
            name_count < UINT32_MAX && (uint64_t)data_capacity.priorities + runtime.priorities + structor_count < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = valid ? (uint32_t)name_count : 0,
        .atoms = valid ? (uint32_t)atom_count : 0,
        .symbols = valid ? (uint32_t)symbol_count : 0,
        .fixups = valid ? (uint32_t)fixup_count : 0,
        .wide_addends = valid ? (uint32_t)wide_count : 0,
        .priorities = valid ? data_capacity.priorities + runtime.priorities + structor_count : 0,
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
            given[s] = renamed.length != 0 ? check_program_emit_given_name(&result->link, &names, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length, true) :
                                             check_program_emit_given_name(&result->link, &names, intern_bytes(identifiers, identifier),
                                                                           intern_length(identifiers, identifier), false);
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
            // Weak where a weak attribute makes it so, or a weakref's target that the unit doesn't define is referred to
            // through weakrefs alone (#147), whatever linkage its own declarations give it (the reader refuses weak on
            // one of internal linkage).
            flags = kind | (program->input->linkage[declaration] != 2 && !weak[s + 1] ? BUSTER_LINK_SYMBOL_LOCAL :
                            weak[s + 1]                                      ? BUSTER_LINK_SYMBOL_WEAK :
                                                                               BUSTER_LINK_SYMBOL_GLOBAL);
            if (program->input->linkage[declaration] == 2 && program->input->object_visibility != NULL && program->input->object_visibility[object] != 0)
            {
                flags |= (uint8_t)((program->input->object_visibility[object] - 1) << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
            }
            // An alias's symbol (#147) has its entry's type with its own binding and visibility; an ifunc's is global
            // whatever its declaration's linkage or weak attribute, as clang gives every ifunc external linkage; and a
            // weakref's own one is a weak reference to its name, of default visibility.
            const uint32_t entry = symbol_aliases != 0 ? symbol_aliases[s + 1] : 0;
            const CheckProgramAlias* alias = entry != 0 ? program->aliases + entry - 1 : NULL;
            flags = alias == NULL ? flags :
                    alias->kind == CHECK_PROGRAM_WEAKREF ? (uint8_t)(kind | BUSTER_LINK_SYMBOL_WEAK) :
                    alias->kind == CHECK_PROGRAM_IFUNC ? (uint8_t)(BUSTER_LINK_SYMBOL_IFUNC | BUSTER_LINK_SYMBOL_GLOBAL | (flags & BUSTER_LINK_SYMBOL_VISIBILITY)) :
                                                         (uint8_t)(alias->type | (flags & (BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_VISIBILITY)));
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
    // Constant automatic initializers have one local readonly symbol per pooled byte image, before runtime symbols.
    for (uint32_t p = 0; valid && p < pools->count; p += 1)
    {
        char spelling[48];
        const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_initializer_%u", p);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT,
                                            BUSTER_LINK_SOURCE_NONE) == pools->symbol_base + p;
        if (valid && binding != NULL)
        {
            result->link.symbols.source[pools->symbol_base + p] = BUSTER_LINK_SOURCE_PROGRAM;
        }
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
    // The jump tables' symbols, then their labels' (CheckProgramLowered.case_symbol_base): local, with names a source
    // can't spell, which -c's object leaves out as an assembler leaves clang's .LJTI and .LBB labels (check_driver_object).
    for (uint32_t k = 0; valid && k < switch_symbols; k += 1)
    {
        const bool table = k < result->lowered.table_count;
        char spelling[48];
        const int length = snprintf(spelling, sizeof(spelling), table ? ".Lbuster_switch_%u" : ".Lbuster_case_%u", table ? k : k - result->lowered.table_count);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        const uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | (table ? BUSTER_LINK_SYMBOL_OBJECT : BUSTER_LINK_SYMBOL_NOTYPE);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == result->lowered.case_symbol_base + k;
    }
    // The asm templates' own symbols, in their names' order, neither typed: a label local, the program's definition
    // (bind.c's), and an undefined name global.
    for (uint32_t n = 0; valid && n < asms->name_count; n += 1)
    {
        if (asm_symbols[n] >= asm_first)
        {
            const String8 spelling = asms->names[n].spelling;
            const NameId name = link_add_name(&result->link, (const uint8_t*)spelling.pointer, (uint32_t)spelling.length);
            const uint8_t flags = BUSTER_LINK_SYMBOL_NOTYPE | (asms->names[n].label ? BUSTER_LINK_SYMBOL_LOCAL : BUSTER_LINK_SYMBOL_GLOBAL);
            valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == asm_symbols[n];
            if (valid && binding != NULL && asms->names[n].label)
            {
                result->link.symbols.source[asm_symbols[n]] = BUSTER_LINK_SOURCE_PROGRAM;
            }
        }
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
    // The aliases' and ifuncs' symbols (#147), which check_program_sections defines once the static data's atoms exist:
    // the program's in -c's link too, so that the static data may refer to a local one before it has an atom
    // (check_data_emit's local targets).
    for (SymbolId s = 1; valid && symbol_aliases != 0 && s <= program->symbol_count; s += 1)
    {
        if (symbol_aliases[s] != 0 && program->aliases[symbol_aliases[s] - 1].kind != CHECK_PROGRAM_WEAKREF)
        {
            result->link.symbols.source[s] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    for (uint32_t t = 0; valid && binding != NULL && t < result->lowered.table_count; t += 1)
    {
        result->link.symbols.source[result->lowered.case_symbol_base + t] = BUSTER_LINK_SOURCE_PROGRAM;
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
            // A naked function's references are its templates' names (mapped below), which no runtime function's are.
            const Tier0Code* code = run.codes + b;
            const bool naked = asms->naked != 0 && asms->naked[b].planned;
            for (uint32_t i = 0; !naked && i < code->reference_count; i += 1)
            {
                const uint64_t provisional = (uint64_t)code->references[i].target - shared.provisional_runtimes;
                code->references[i].target = provisional < CHECK_LOWER_RUNTIMES ? result->lowered.runtime_symbols[provisional] : code->references[i].target;
            }
        }
    }
    // Then every reference of a jump table's provisional symbol takes the table's, in the functions that have tables
    // (CheckLowerTable.function, in their order). In this order each reference is rewritten once: a runtime function's
    // symbol is below the tables' provisional ones, while a table's final symbol is among the runtime functions'.
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        const uint32_t b = result->lowered.tables[t].function;
        const Tier0Code* code = run.codes + b;
        for (uint32_t i = 0; (t == 0 || result->lowered.tables[t - 1].function != b) && i < code->reference_count; i += 1)
        {
            const uint64_t table = (uint64_t)code->references[i].target - shared.provisional_tables;
            code->references[i].target = table < result->lowered.table_count ? result->lowered.case_symbol_base + (SymbolId)table : code->references[i].target;
        }
    }
    // Every reference of an asm template's (an asm helper's or a naked function's) takes its name's symbol: its target is
    // the name's index (check_program_asm_names).
    for (uint32_t f = 0; valid && asms->name_count != 0 && f < count + helpers->count; f += 1)
    {
        const bool assembly = f < count ? asms->naked != 0 && asms->naked[f].planned : helpers->items[f - count].assembly != 0;
        const Tier0Code* code = f < count ? run.codes + f : &helpers->items[f - count].code;
        for (uint32_t i = 0; valid && assembly && i < code->reference_count; i += 1)
        {
            valid = code->references[i].target < asms->name_count;
            code->references[i].target = valid ? asm_symbols[code->references[i].target] : 0;
        }
    }
    // The file-scope assembly's atoms, before the functions', as clang's assembly output has a module's asm first.
    result->asm_atoms = valid && file_asm.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, file_asm.count) : 0;
    valid = valid && (file_asm.count == 0 || result->asm_atoms != 0);
    for (uint32_t a = 0; valid && a < file_asm.count; a += 1)
    {
        result->asm_atoms[a] = link_add_atom(&result->link, file_asm.classes[a], file_asm.alignments[a], file_asm.sizes[a], file_asm.bytes[a], 0);
        valid = result->asm_atoms[a] != 0;
    }
    result->asm_sections = valid ? file_asm.sections : 0;
    result->asm_atom_count = valid ? file_asm.count : 0;
    // The bodies' atoms, symbols and fixups in source order, each body's code as its lane made it, then the helpers', which
    // are encoded here: the labels' symbols, each function's after the ones before it (CheckProgramLowered.label_symbols),
    // and with unwind tables each function's frame, as its code is sized.
    check_clock_mark(stage_clock, CHECK_STAGE_EMIT_SETUP);
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
        // An asm template's code: a naked function's or an asm statement's helper's, whose labels are defined below.
        const bool assembly = source_body ? asms->naked != 0 && asms->naked[b].planned : helpers->items[b - count].assembly != 0;
        uint32_t* offsets = 0;
        if (!source_body)
        {
            // A helper's code, encoded on the calling thread as a body's is on its lane, its frame after the bodies'.
            helper_body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK };
            offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
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
                const BranchSizingCheck shape = BUSTER_SCALAR_PATH(scalar) ? branch_sizing_validate_branches_scalar(code->records, code->count, code->branches, code->targets, code->branch_count) :
                                                         branch_sizing_validate_branches(code->records, code->count, code->branches, code->targets, code->branch_count);
                const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
                const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                                       BUSTER_SCALAR_PATH(scalar) ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                                x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
                encodes = length >= 0;
                if (encodes && unwind)
                {
                    encodes = length <= INT32_MAX && unwind_tier0_frame(code, offsets, (uint32_t)length, &run.frames[b]) == UNWIND_OK;
                    UnwindFunction proven;
                    const UnwindError analyzed = !encodes || !verify ? UNWIND_ERROR_UNSUPPORTED :
                                                 BUSTER_SCALAR_PATH(scalar) ? unwind_tier0_scalar(code, offsets, (uint32_t)length, &proven) :
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
                    link_add_code_fixups(&result->link, atom, code->records, code->count, body->fixup_ends,
                                         assembly ? check_program_emit_marked(code) : code->reference_count, code->references, code->reference_count);
            // The labels its templates define, each at its record's offset (its lane's, for a naked function): the
            // program's definitions.
            const CheckProgramAsmLabel* asm_labels = !assembly ? 0 : source_body ? asms->naked[b].labels : asms->plans[helpers->items[b - count].assembly - 1].labels;
            const uint32_t asm_label_count = !assembly ? 0 : source_body ? asms->naked[b].label_count : asms->plans[helpers->items[b - count].assembly - 1].label_count;
            for (uint32_t k = 0; valid && k < asm_label_count; k += 1)
            {
                const uint32_t record = asm_labels[k].record;
                const uint32_t offset = source_body ? body->asm_offsets[k] : record < code->count ? offsets[record] : record == code->count ? (uint32_t)body->length : UINT32_MAX;
                const SymbolId label = asm_symbols[asm_labels[k].name];
                valid = offset != UINT32_MAX && link_define_symbol(&result->link, label, atom, offset, 0);
                if (valid)
                {
                    result->link.symbols.source[label] = BUSTER_LINK_SOURCE_PROGRAM;
                }
            }
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
    // Copy bytes into the byte arena, whose lifetime and rollback contract cover every published atom.
    for (uint32_t p = 0; valid && p < pools->count; p += 1)
    {
        const CheckLowerInitPool* pool = pools->items + p;
        uint8_t* data = arena_push(bytes, pool->size, 1);
        const AtomId atom = data != NULL ? link_add_atom(&result->link, BUSTER_LINK_CLASS_RODATA, 0, pool->size,
                                                       memcpy(data, pool->bytes, pool->size), 0) : 0;
        valid = atom != 0 && link_define_symbol(&result->link, pools->symbol_base + p, atom, 0, pool->size);
        if (valid)
        {
            result->link.symbols.source[pools->symbol_base + p] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_CODE_ATOMS);
    // The unwind tables, after the last function: measured, then written into the byte arena, one CIE and an FDE per
    // function after it, as one EH_FRAME atom whose FDEs' initial locations refer to their functions.
    if (valid && frames != 0)
    {
        UnwindOutput tables = { .location_capacity = (uint32_t)frames, .shared = true };
        valid = unwind_write(run.frames, (uint32_t)frames, &tables, BUSTER_SCALAR_PATH(scalar)) == UNWIND_ERROR_CAPACITY;
        uint32_t* locations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, frames) : 0;
        uint8_t* table_bytes = valid ? arena_push(bytes, tables.size, 8) : 0;
        tables.bytes = table_bytes;
        tables.capacity = tables.size;
        tables.locations = locations;
        valid = locations != 0 && table_bytes != 0 && unwind_write(run.frames, (uint32_t)frames, &tables, BUSTER_SCALAR_PATH(scalar)) == UNWIND_OK;
        const AtomId atom = valid ? link_add_atom(&result->link, BUSTER_LINK_CLASS_EH_FRAME, 3, tables.size, table_bytes, 0) : 0;
        valid = atom != 0;
        for (uint32_t f = 0; valid && f < frames; f += 1)
        {
            const SymbolId symbol = f < count ? program->object_symbols[program->bodies[f].object] : helpers->items[f - count].symbol;
            valid = link_add_fixup(&result->link, atom, locations[f], BUSTER_LINK_FIXUP_PC32, symbol, 0) != 0;
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_UNWIND);
    // Each jump table, in .rodata: an entry per value, its label's offset from the table, which a PC32 fixup gives
    // (S + A - P, P the entry's place, A its offset in the table), as clang's .long .LBB - .LJTI entries are.
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        const CheckLowerTable* table = result->lowered.tables + t;
        const uint64_t size = (uint64_t)table->count * 4;
        uint8_t* data = arena_push(bytes, size, 4);
        const AtomId atom = data != 0 ? link_add_atom(&result->link, BUSTER_LINK_CLASS_RODATA, 2, size, (uint8_t*)memset(data, 0, size), 0) : 0;
        const SymbolId symbol = result->lowered.case_symbol_base + t;
        const SymbolId labels_base = result->lowered.case_symbol_base + result->lowered.table_count;
        valid = atom != 0 && link_define_symbol(&result->link, symbol, atom, 0, size);
        for (uint32_t i = 0; valid && i < table->count; i += 1)
        {
            valid = link_add_fixup(&result->link, atom, i * 4, BUSTER_LINK_FIXUP_PC32, labels_base + table->entries[i], (int64_t)i * 4) != 0;
        }
        if (valid)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    // The constructors' and destructors' entries (#147): a pointer to the function each, in an init or fini array atom
    // of its own, in the order LLVM writes llvm.global_ctors and llvm.global_dtors: stably by priority, from the
    // definitions' order. One below 65535 has its priority (-c's .init_array.N and .fini_array.N, check_program_sections),
    // and the others are their arrays' own.
    if (valid && structor_count != 0)
    {
        uint64_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, structor_count);
        uint64_t* sorting = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, structor_count);
        valid = order != 0 && sorting != 0;
        uint32_t entries = 0;
        for (uint32_t b = 0; valid && b < count; b += 1)
        {
            const SymbolId symbol = program->object_symbols[program->bodies[b].object];
            for (uint32_t k = 0; k < 2; k += 1)
            {
                if (structors[2 * symbol + k] != 0)
                {
                    order[entries++] = ((uint64_t)((k << 16) | (structors[2 * symbol + k] - 1)) << 32) | b;
                }
            }
        }
        const uint64_t* sorted = valid ? c_bindings_sort(order, sorting, structor_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : 0;
        for (uint32_t e = 0; valid && e < structor_count; e += 1)
        {
            const uint32_t k = (uint32_t)(sorted[e] >> 48);
            const uint32_t priority = (uint32_t)(sorted[e] >> 32) & 0xffff;
            const SymbolId symbol = program->object_symbols[program->bodies[(uint32_t)sorted[e]].object];
            uint8_t* data = arena_push(bytes, 8, 8);
            const AtomId atom = data != 0 ? link_add_atom(&result->link, k != 0 ? BUSTER_LINK_CLASS_FINI_ARRAY : BUSTER_LINK_CLASS_INIT_ARRAY, 3, 8,
                                                       (uint8_t*)memset(data, 0, 8), 0) : 0;
            valid = atom != 0 && link_add_fixup(&result->link, atom, 0, BUSTER_LINK_FIXUP_ABS64, symbol, 0) != 0 &&
                    (priority == 65535 || link_add_priority(&result->link, atom, priority));
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_CODE_ATOMS);
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
    // With --tier1-refusals, each body Tier 0 compiled, in source order, by its symbol's name.
    for (uint32_t b = 0; valid && run.tier1_outcomes != 0 && lanes->tier1_refusals != NULL && b < count; b += 1)
    {
        const SymbolId symbol = program->object_symbols[program->bodies[b].object];
        const NameId name = symbol < result->link.symbols.count ? result->link.symbols.name[symbol] : 0;
        if ((run.tier1_outcomes[b] & 0xff) != CHECK_TIER1_COMPILED && (run.tier1_outcomes[b] & 0xff) < CHECK_TIER1_STAGE_COUNT && name != 0)
        {
            check_tier1_print_refusal(lanes->tier1_refusals, result->link.names.bytes[name], result->link.names.length[name], run.tier1_outcomes[b]);
        }
    }
    arena_pop_to(scratch, temporary);
    arena_release(&own_columns);
    if (timing)
    {
        lanes->merge_ns = check_program_lanes_now() - lanes_end;
    }
    return valid;
}

// check_program_emit_timed without a clock: the benchmark's and the tests' emission on lanes.
static bool check_program_emit_lanes(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                                     const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                                     CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify, CheckProgramLanes* lanes)
{
    (void)scalar;

    return check_program_emit_timed(result, program, parser, identifiers, data_capacity, BUSTER_SCALAR_PATH(scalar), output, bytes, scratch, binding, rows, unwind, verify, lanes,
                                    NULL);
}

// check_program_emit_lanes on the calling thread alone.
static bool check_program_emit(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                               const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                               CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify)
{
    (void)scalar;

    return check_program_emit_lanes(result, program, parser, identifiers, data_capacity, BUSTER_SCALAR_PATH(scalar), output, bytes, scratch, binding, rows, unwind, verify, NULL);
}

// Whether `name` is `prefix`, or starts with it and a '.', as LLVM's section names group (hasPrefix).
static bool check_program_section_family(String8 name, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return name.length >= length && memcmp(name.pointer, prefix, length) == 0 && (name.length == length || name.pointer[length] == '.');
}

// Whether `name` starts with `prefix`.
static bool check_program_section_prefix(String8 name, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return name.length >= length && memcmp(name.pointer, prefix, length) == 0;
}

// GNU's aliases and ifuncs (#147), once every atom of the unit's is in the link: each one's symbol defined at its base's
// atom and place, with its size, as an assembler defines clang's `.set alias, base` (LLVM's ELF writer gives such a
// symbol its base's section and value, and its size where it has none of its own, which clang's hasn't). Returns false,
// with `code`'s status, reason and token, where a base has no atom (its definition wasn't emitted).
static bool check_program_define_aliases(CheckProgramCode* code, const CheckProgram* program)
{
    Link* link = &code->link;
    bool valid = true;
    for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const SymbolId taken = alias->kind != CHECK_PROGRAM_WEAKREF ? program->object_symbols[alias->object] : 0;
        const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == alias->object ? taken : 0;
        const SymbolId base = symbol != 0 && alias->base != 0 ? program->object_symbols[alias->base] : 0;
        const AtomId atom = base != 0 && base < link->symbols.count ? link->symbols.atom[base] : 0;
        valid = symbol == 0 || (atom != 0 && link_define_symbol(link, symbol, atom, link->symbols.value[base], link->symbols.size[base]));
        if (valid && symbol != 0)
        {
            link->symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
        else if (!valid)
        {
            code->status = atom == 0 ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
            code->reason = atom == 0 ? "an alias or ifunc whose definition isn't emitted" : "an alias's or ifunc's symbol couldn't be defined at its definition";
            code->token = alias->attribute;
        }
    }
    return valid;
}

// The named sections of the unit's emitted definitions (#619), once its static data is published, for -c's object
// (ObjectOptions.atom_sections) and the direct path alike: each definition CheckInputSections.placements names a
// section for, which the unit defines in an atom of the link, gets a descriptor of that name, of the kind LLVM gives a
// global with an explicit section (TargetLoweringObjectFileELF's getExplicitSectionGlobal): a function's text (AX); a
// variable's by its atom's class, data (WA), read-only (A), read-only after relocation (WA) or thread-local data (WAT),
// or by its name's family, .bss's (WA, NOBITS) or .tbss's (WAT, NOBITS) for a zero-filled one. A zero-filled variable
// in another section's name is data, as LLVM doesn't put a global with an explicit section in BSS: its atom becomes
// zeroed bytes of the class it is in (DATA or TDATA), in `bytes`. A name another kind's section has (a function's
// .data) is a section of its own with this kind's flags, as LLVM makes it. The file-scope assembly's atoms keep their
// own (CheckProgramCode.asm_sections). Refused by name: a name LLVM gives another kind than the definition's (an
// initialized variable in a .bss, which clang's code generation refuses too, a function in a .tdata), and one it gives
// another type (.note, the arrays'), or the conservatively reserved .llvm.* family (including ordinary PROGBITS names).
// `*table` is null for a unit without either, and otherwise the descriptors by AtomId,
// in `output`. Then, every atom in place, the aliases' and ifuncs' symbols are defined at their bases' (#147,
// check_program_define_aliases): every caller that publishes the static data calls this after it. Returns false with
// `code`'s status, reason and token set.
static bool check_program_sections(CheckProgramCode* code, const CheckProgram* program, Arena* bytes, Arena* output, ObjectAtomSection** table)
{
    const CheckInputSections* sections = program->input->sections;
    const uint32_t placements = sections != NULL ? sections->placement_count : 0;
    Link* link = &code->link;
    *table = 0;
    bool valid = true;
    if (placements != 0 || code->asm_atom_count != 0 || link->priorities.count > 1 || code->retained_count != 0)
    {
        uint32_t refused = BUSTER_CHECK_NONE;
        const char* reason = NULL;
        ObjectAtomSection* descriptors = BUSTER_ARENA_PUSH_ARRAY(output, ObjectAtomSection, link->atoms.count);
        valid = descriptors != 0;
        code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
        if (valid)
        {
            memset(descriptors, 0, (uint64_t)link->atoms.count * sizeof(ObjectAtomSection));
            for (uint32_t a = 0; a < code->asm_atom_count; a += 1)
            {
                descriptors[code->asm_atoms[a]] = code->asm_sections[a];
            }
            // A prioritized constructor's or destructor's entry (#147) is in .init_array.N or .fini_array.N, as clang's
            // object has it (TargetLoweringObjectFileELF's getStaticCtorSection), of its array's type and flags.
            for (uint32_t p = 1; valid && p < link->priorities.count; p += 1)
            {
                const AtomId atom = link->priorities.atom[p];
                const uint8_t class = link->atoms.class[atom];
                char* name = BUSTER_ARENA_PUSH_ARRAY(output, char, 24);
                valid = name != 0;
                const int length = valid ? snprintf(name, 24, "%s.%u", object_class_sections[class].name, (unsigned)link->priorities.priority[p]) : 0;
                descriptors[atom] = (ObjectAtomSection){ .name = { .pointer = name, .length = (uint64_t)length }, .type = object_class_sections[class].type,
                                                         .flags = object_class_sections[class].flags };
            }
            code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
            *table = descriptors;
        }
        for (uint32_t p = 0; valid && p < placements; p += 1)
        {
            const CheckInputSectionPlacement* placement = sections->placements + p;
            const uint32_t object = placement->token < program->input->token_count ? program->input->objects[placement->token] : 0;
            // The object's own symbol, defined: not a weakref's target's (#147), nor an alias's or ifunc's, which clang
            // keeps in its base's section, and which check_program_define_aliases defines after the placements.
            const SymbolId taken = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
            const AtomId atom = symbol != 0 && symbol < link->symbols.count ? link->symbols.atom[symbol] : 0;
            const String8 name = placement->name;
            const uint8_t class = atom != 0 ? link->atoms.class[atom] : BUSTER_LINK_CLASS_NONE;
            // The kind its name gives it, if any (getELFKindForNamedSection), and the class it then goes in.
            const bool bss = check_program_section_family(name, ".bss") || check_program_section_prefix(name, ".gnu.linkonce.b.") ||
                             check_program_section_prefix(name, ".llvm.linkonce.b.") || check_program_section_family(name, ".sbss") ||
                             check_program_section_prefix(name, ".gnu.linkonce.sb.") || check_program_section_prefix(name, ".llvm.linkonce.sb.");
            const bool tdata = check_program_section_family(name, ".tdata") || check_program_section_prefix(name, ".gnu.linkonce.td.") ||
                               check_program_section_prefix(name, ".llvm.linkonce.td.");
            const bool tbss = check_program_section_family(name, ".tbss") || check_program_section_prefix(name, ".gnu.linkonce.tb.") ||
                              check_program_section_prefix(name, ".llvm.linkonce.tb.");
            // Keep the entire .llvm.* family unsupported, including names Clang treats as ordinary PROGBITS. This
            // is a conservative reserved-name boundary, separate from the names ELF assigns another section type.
            const bool restricted = check_program_section_prefix(name, ".note") || check_program_section_family(name, ".init_array") ||
                                    check_program_section_family(name, ".fini_array") || check_program_section_family(name, ".preinit_array") ||
                                    check_program_section_prefix(name, ".llvm.");
            const uint8_t placed = class == BUSTER_LINK_CLASS_BSS && !bss ? BUSTER_LINK_CLASS_DATA : class == BUSTER_LINK_CLASS_TBSS && !tbss ? BUSTER_LINK_CLASS_TDATA : class;
            const bool fits = restricted ? false :
                              bss ? class == BUSTER_LINK_CLASS_BSS :
                              tdata ? class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_TBSS :
                              tbss ? class == BUSTER_LINK_CLASS_TBSS :
                                     class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_DATA || class == BUSTER_LINK_CLASS_BSS ||
                                     class == BUSTER_LINK_CLASS_RODATA || class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_TDATA ||
                                     class == BUSTER_LINK_CLASS_TBSS;
            const ObjectSectionFormat* format = &object_class_sections[placed];
            const ObjectAtomSection descriptor = {
                .name = name,
                .type = format->type,
                .flags = format->flags,
            };
            if (atom != 0 && !fits)
            {
                // The placements are in source order but for the unit's end's tentative definitions, after the rest:
                // the refusal is the earliest's.
                refused = placement->token < refused ? placement->token : refused;
                reason = placement->token == refused ? (restricted ? "a section name with an unsupported ELF type or reserved family" :
                                                                "a definition in a section whose name gives it another kind") :
                                                       reason;
            }
            // An empty section attribute clears inherited placement and overrides pragma settings, but LLVM keeps
            // the definition in its default class. Retain its reader event for Sema's conflicts without materializing
            // a zero-filled BSS/TBSS atom as data for a descriptor that the object writer treats as absent.
            else if (atom != 0 && name.length != 0)
            {
                if (placed != class)
                {
                    // A zero-filled variable, as data in its section: its atom's zeroed bytes.
                    const uint64_t size = link->atoms.size[atom];
                    uint8_t* zeros = size != 0 ? arena_push(bytes, size, 16) : 0;
                    valid = size == 0 || zeros != 0;
                    code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
                    if (valid)
                    {
                        if (zeros != 0)
                        {
                            memset(zeros, 0, size);
                        }
                        link->atoms.class[atom] = placed;
                        link->atoms.bytes[atom] = zeros;
                    }
                }
                descriptors[atom] = descriptor;
            }
        }
        // A retained definition (#147) keeps its section, SHF_GNU_RETAIN: its named section's, or one of its own, its
        // class's section's name and its symbol's ("<section>.<name>", as LLVM names a retained global's unique section,
        // getELFSectionNameForGlobal), of its class's type and flags.
        for (uint32_t r = 0; valid && refused == BUSTER_CHECK_NONE && r < code->retained_count; r += 1)
        {
            const SymbolId symbol = code->retained_symbols[r];
            const AtomId atom = link->symbols.atom[symbol];
            const NameId name = link->symbols.name[symbol];
            const ObjectSectionFormat* format = atom != 0 ? &object_class_sections[link->atoms.class[atom]] : NULL;
            const uint64_t prefix = format != NULL ? strlen(format->name) : 0;
            char* spelled = format != NULL && descriptors[atom].name.length == 0 ? BUSTER_ARENA_PUSH_ARRAY(output, char, prefix + 1 + link->names.length[name]) : 0;
            valid = format == NULL || descriptors[atom].name.length != 0 || spelled != 0;
            code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
            if (valid && spelled != 0)
            {
                memcpy(spelled, format->name, prefix);
                spelled[prefix] = '.';
                memcpy(spelled + prefix + 1, link->names.bytes[name], link->names.length[name]);
                descriptors[atom] = (ObjectAtomSection){ .name = { .pointer = spelled, .length = prefix + 1 + link->names.length[name] }, .type = format->type,
                                                         .flags = format->flags, .entry_size = format->entry_size };
            }
            if (valid && format != NULL)
            {
                descriptors[atom].flags |= SHF_GNU_RETAIN;
            }
        }
        if (valid && refused != BUSTER_CHECK_NONE)
        {
            code->status = CHECK_PROGRAM_UNSUPPORTED;
            code->reason = reason;
            code->token = refused;
            valid = false;
        }
    }
    valid = valid && check_program_define_aliases(code, program);
    // A symbol of internal linkage that no definition emitted is still an undefined reference: clang writes it as a
    // global undefined symbol (GNU ld and lld report it, and an executable link fails), never as an undefined local,
    // which an ELF object can't hold. The definitions' and aliases' symbols above have their sources set.
    for (SymbolId symbol = 1; valid && symbol < code->link.symbols.count; symbol += 1)
    {
        if ((code->link.symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL &&
            code->link.symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE)
        {
            code->link.symbols.flags[symbol] = (uint8_t)((code->link.symbols.flags[symbol] & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_GLOBAL);
        }
    }
    return valid;
}
