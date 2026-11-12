// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// test-check-lanes [--executable PATH] [--seed N] [--programs N] [--id-order N] [--keep]: the functions' lanes (#151, and
// #153's item for them), check_program_emit_lanes. Every output must be one lane's, whatever the lanes, whichever finishes
// first and whatever ids the identifiers get.
//
// - Programs: generated programs (--programs, 6 by default, and two of 600 functions) of functions of many kinds, among
//   them 128-bit integers' division and conversions and complex products (libgcc's functions, which the lowering numbers
//   in the order the bodies first call them), computed gotos through static tables of labels' addresses (the label
//   symbols), switches, structs by value, long doubles, variadic calls and statics, in a random order, each compiled
//   through the pipeline once by each path, then emitted on one lane (check_program_emit) and on 2, 3 and 8 lanes with
//   every lane forced (CheckProgramLanes.every_lane), on 8 with lanes 0, 1 and 7 each slow in turn (so that the others
//   take its parts and finish first), and on 8 that can't be pinned (CPUs past every one lane_pin takes, which lane_run
//   runs unpinned and then counts as lanes that didn't start), with -g's rows and unwind tables and without: each the
//   same object (object_build of the emission's link and the static data), byte for byte, as one lane's, with the lanes
//   and parts the emission says it ran, run again on one lane only when its lanes didn't start.
// - The ids' order (#153): each generated and failing program compiled again by the same path with its identifiers' ids
//   in another order (check_driver_id_order, the driver's --id-order, from --id-order N on, 153 by default), whose table
//   must have every name of the first compile, most of them at other ids, then emitted on one lane and on 8, with -g's
//   rows and unwind tables and without: each the first compile's object or failure on one lane, byte for byte.
// - The lane rule: a program's lanes by default (CheckProgramLanes.lanes 8, not every lane), one for few tokens, and
//   more with more bodies than twice the largest's, never more than the bodies.
// - Failures: programs whose bodies fail at different steps, in different orders (a lowering's refusal by name, Tier 0's
//   refusal, the calling convention's), emitted as above: the same status, token, entry, reason and Tier 0 error at every
//   lane count as one lane's, which are source order's (the steps' order, then the bodies').
// - Squeezes: a lane short of room (CheckProgramLanes.squeeze_body), in a generated program of its own seed and in each
//   failing program, by each path, with -g's rows and unwind tables and without, on 8 lanes: the bodies with the fewest
//   and the most tokens, each squeezed in turn in the output, scratch and bytes arenas of the lane that takes its part,
//   from no room to spare up, doubling, until the part fits. One lane's emission makes the generated program's object
//   and each failing program's failure; each emission on lanes is one lane's, run again on one lane while the part
//   doesn't fit, and the generated program's sweeps reach each of a resource's reasons: the lowering's, Tier 0's and
//   the code's after it.
// - Lanes that don't start: in a child limited to the processes it has (RLIMIT_NPROC, which doesn't limit root), so that
//   lane_run starts no thread, the squeezes' program and each failing program on 8 lanes, as above: one lane's object or
//   failure, run again on one lane. The child's alarm ends it if its lanes hang, which fails the check.
// - The address space: in a child, the squeezes' program by each path, with -g's rows and unwind tables and without. An
//   emission on lanes of one reserves exactly the room lane 0's writer's columns take; then emissions limited (RLIMIT_AS)
//   to less address space than that reservation maps, and to it and 1 GB more, far less than a lane's 64 GB arenas:
//   check_program_emit's, on lanes of one and on 8 lanes, each one lane's object, lane 0's columns in the caller's
//   scratch at the first limit and in their reservation at the second, and the 8 lanes run again on one. And a program
//   without bodies, which reserves nothing for lane 0's columns, emitted with no address space past its arenas.
// - The caller's scratch: the squeezes' program by each path, with -g's rows and unwind tables and without, emitted by
//   check_program_emit and on lanes of one with the least room of the caller's scratch they take, found by halving:
//   one lane's object, with less room than lane 0's columns take, whose own arena mustn't depend on the scratch's room.
// - The driver (with --executable, this executable by default): four of the generated programs' objects (-c) and
//   executables (one of 3 functions, two of random sizes and the first of 600) by --lanes 1, 3 and 8 and by default, and
//   by --lanes 8 with the ids in another order (--id-order), by each path: the same bytes, and the executables exit with
//   the same status; in a child limited as above, the first and the last of them by --lanes 8, each one lane's; each
//   failing program by --lanes 8, with lanes, with the ids in another order and in such a child, by each path: the exit
//   status and diagnostics of --lanes 1; and --lanes', --worker-budget's and --id-order's values it refuses.
// - The driver's deferred bodies (--deferred-bodies, check_input_deferred_compile): the checker reads a unit's bodies
//   apart, on rounds of the command's flow. The same four programs by the vector path, by --lanes 1, 2, 3 and 8, by
//   default and by --lanes 8 with the ids in another order: source order's objects and executables, byte for byte, the
//   executables exiting alike; the program of 600 functions by --lanes 8 with --time-stages, its bodies read on more
//   than one lane (with more than one CPU) and merged, by --lanes 1 in source order, after a `#pragma clang section`
//   line in source order before its top level's walk, and by --no-deferred-bodies after --deferred-bodies in source
//   order; under a jobserver pipe of none, 1, 3 and 7 spare tokens, on a lane per token up to the CPUs (none: source
//   order), every token back in the pipe once, and by --worker-budget 1 and 3 on no more lanes than the budget, each
//   object source order's; a unit of 16-byte lock-free queries by -mcx16, source order's object, which -mno-cx16's isn't;
//   in a child limited as above, its object by --lanes 8 source order's; and each
//   failing program by --lanes 2, 3 and 8, and in such a child: the exit status and diagnostics of --lanes 1.
// - The driver's session's threads (#905): a library preloaded into the driver counts its calls of pthread_create,
//   which its session makes once, before any source is read or, by default for one source file, once it is
//   preprocessed (#1444), on the first program of 600 functions: none for --lanes 1, --worker-budget=1 and a source's
//   -fsyntax-only, and an object compiled on one CPU; one per lane past the first of --lanes 3 and, by default, of the
//   cores of the CPUs it may run on (by /proc/cpuinfo, lane_test_process_cores), for an object, an executable and two
//   sources' objects, whatever lanes their emissions take; where the process has a core's two siblings, on them by
//   default one lane, its lane 1 created to count the core, and two by --lanes 2, and on CPUs of two cores two; and
//   when its second lane fails to start (BUSTER_TEST_LANE_FAIL_AT), the one created, after which the command runs on one
//   lane, writing --lanes 1's object; and none by default for the program of three functions' object and executable,
//   too small for a second lane to pay. --time-stages reports the lanes asked for and those run on: 3 and 3 for --lanes
//   3, the cores' for the default, 4 and 1 for that failure, and 1 and 1 for the small program's object.
// - The exit helper (#1444, lane_exit_helper_start): the small program's object and -E, a #warning's -fsyntax-only, a
//   failing object and the 600 functions' object, each by BUSTER_EXIT_HELPER=0, =1 and by default, this process a
//   subreaper for each run: the same status, output, errors and object each way; one helper by =1 where the driver's
//   lanes created no thread (none in a sanitizer build), by default where PID 1 reaps orphans too (lane_exit_reaped),
//   none otherwise, each ended within BUSTER_CHECK_LANES_EXIT_SECONDS of the driver; and the small object by =1 with
//   the driver held after its helper's start (BUSTER_TEST_EXIT_HELPER_PAUSE), whose streams end while it still runs.
// - A growing team (#673's tail). The growth gate alone (check_program_emit_grow), under a jobserver pipe of 7 tokens:
//   parts left of one token fewer than pay for a guardian don't grow a team of 1 of 8 lanes, and the least that pays
//   grows it by 7; --lanes N grows it whatever they pay; with every part lowered, their code's share of the tokens
//   decides, one token short and the least that pays; the items left, the lanes left and the spare tokens each bound
//   the growth; no item left, none; a guardian only for a growth, and the tokens back after each; and a guardian that
//   took no token, from a reader whose bytes FIONREAD counts but no guardian takes (a file's), released at once rather
//   than left holding one of the team's leases. Then the driver: the first program of 600 functions by --lanes 4, and
//   programs of 4,000 and 200 functions by the default policy (with --worker-budget 3 and without), under a jobserver
//   pipe of no token or one, its lane 0 writing none, 2, 3 or 5 more after its first item (BUSTER_TEST_WORKER_SUPPLY),
//   as another recipe returning them would: --time-stages reports the lanes the lane rule asked for, those the
//   jobserver granted as it started, the lanes it ran on at its end and those it grew by, up to its lanes and the cap,
//   none for the 200 functions, which ask for more than one lane but whose parts don't pay for a guardian, and no more
//   lanes that took a part than it had admitted (with one token and none supplied, 2 of a round of 4); its object is
//   --lanes 1's; and afterwards the pipe holds each token once, the supplied ones among them, those it had no lane for
//   never taken.
// - Commands in one process (#905): a session of 4 lanes whose flow runs, three times, a command that fails, one that
//   writes an object on 4 lanes, one on 2 by --worker-budget 2, a -fsyntax-only, and -fsyntax-only of two sources by 3
//   lanes and by 1, every lane calling each (run_check_command's collective call): each lane the same thread
//   throughout, the session's barrier advancing, no thread created, the failure's status and each object --lanes 1's in
//   a process of its own; then the same on a lazy session, whose lane 1 creates the others while lane 0 runs the first
//   command. And, with 4 CPUs or more, a growing team's command on a session of 4 lanes in this process, under a
//   jobserver pipe without a token whose lane 0 supplies 3: it grows to 4 lanes, and with --worker-budget 2, which the
//   session's 4 lanes don't bound, to 2; and with its lane 1 asleep for 20 ms before each part's lowering it takes
//   (BUSTER_TEST_SLOW_LANE), so that the other lanes reach that part's code before its lowering is done and must wait
//   for it; the pipe holds the 3 once as the command returns, while anything it leaked would still hold them; the object
//   is --lanes 1's; and the caller's MAKEFLAGS and test hooks are as they were.

#include "buster/compiler/test_paths.h"
#include "worker_budget_test.c"

typedef struct CheckLanesTestTally CheckLanesTestTally;
struct CheckLanesTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void check_lanes_test_expect(CheckLanesTestTally* tally, bool condition, const char* what, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 24)
        {
            fprintf(stderr, "test-check-lanes: FAIL: %s%s%s\n", what, detail != 0 ? ": " : "", detail != 0 ? detail : "");
        }
    }
}

static uint64_t check_lanes_test_random(CheckLanesTestTally* tally)
{
    tally->random ^= tally->random << 13;
    tally->random ^= tally->random >> 7;
    tally->random ^= tally->random << 17;
    return tally->random;
}

static __attribute__((format(printf, 4, 5))) void check_lanes_test_append(char* text, uint64_t capacity, uint64_t* used, const char* format, ...)
{
    if (*used < capacity)
    {
        va_list arguments;
        va_start(arguments, format);
        const int written = vsnprintf(text + *used, capacity - *used, format, arguments);
        va_end(arguments);
        *used += written > 0 ? (uint64_t)written : 0;
    }
}

// The kinds of function a generated program has: each takes two longs and returns one, the same for any input.
#define BUSTER_CHECK_LANES_TEST_KINDS 12

// Function `index` of kind `kind`, which may call function `callee` (one before it, or none for UINT32_MAX).
static void check_lanes_test_function(char* text, uint64_t capacity, uint64_t* used, uint32_t index, uint32_t kind, uint32_t callee)
{
    check_lanes_test_append(text, capacity, used, "static long f%u(long a, long b)\n{\n", index);
    if (kind == 0)
    {
        check_lanes_test_append(text, capacity, used,
                                "    long r = a;\n"
                                "    for (int i = 0; i < 5; i += 1)\n    {\n        r = r * 3 + b - i;\n        if (r > 100000)\n        {\n            r -= 99991;\n        }\n    }\n");
    }
    else if (kind == 1)
    {
        // 128-bit division and remainder: libgcc's __divti3 and __modti3.
        check_lanes_test_append(text, capacity, used,
                                "    __int128 x = (__int128)a * 1000003 + b;\n    __int128 y = (__int128)(b | 1);\n    long r = (long)(x / y) + (long)(x %% y);\n");
    }
    else if (kind == 2)
    {
        // Unsigned: __udivti3 and __umodti3.
        check_lanes_test_append(text, capacity, used,
                                "    unsigned __int128 x = (unsigned __int128)(unsigned long)a * 77777 + (unsigned long)b;\n"
                                "    long r = (long)(x / ((unsigned long)b | 3)) ^ (long)(x %% 7);\n");
    }
    else if (kind == 3)
    {
        // A complex product: __muldc3.
        check_lanes_test_append(text, capacity, used,
                                "    _Complex double z = a;\n    __imag__ z = (double)b;\n    z = z * z;\n    long r = (long)__real__ z - (long)__imag__ z;\n");
    }
    else if (kind == 4)
    {
        // Conversions between 128-bit integers and doubles: __floattidf and __fixdfti.
        check_lanes_test_append(text, capacity, used,
                                "    __int128 x = (__int128)a * 3 + b;\n    double d = (double)x;\n    __int128 y = (__int128)(d * 0.5);\n    long r = (long)y;\n");
    }
    else if (kind == 5)
    {
        // A computed goto through a static table of its labels' addresses: label symbols.
        check_lanes_test_append(text, capacity, used,
                                "    static void* targets[] = { &&first, &&second, &&third };\n"
                                "    long r = a;\n    long i = 0;\n    goto *targets[b & 1];\n"
                                "first:\n    r += 3;\n    i += 1;\n    if (i < 3)\n    {\n        goto *targets[2];\n    }\n    goto done;\n"
                                "second:\n    r *= 2;\n    goto first;\n"
                                "third:\n    r -= 1;\n    goto *targets[0];\n"
                                "done:\n");
    }
    else if (kind == 6)
    {
        check_lanes_test_append(text, capacity, used,
                                "    long image[65] = { [0 ... 64] = 17 };\n    long r = 0;\n    switch (a & 7)\n    {\n"
                                "        case 0: r = b; break;\n        case 1: r = b * 2; break;\n        case 2: r = b - a; break;\n"
                                "        case 3: r = a * a; break;\n        case 5: r = b ^ 5; break;\n        default: r = -b; break;\n    }\n"
                                "    r += image[(unsigned long)b & 63];\n");
    }
    else if (kind == 7)
    {
        check_lanes_test_append(text, capacity, used, "    struct pair p = { a, b };\n    struct pair q = swap_pair(p);\n    long r = q.x * 2 - q.y;\n");
    }
    else if (kind == 8)
    {
        check_lanes_test_append(text, capacity, used, "    long double x = a;\n    x = x * 1.5L + b;\n    long r = (long)x;\n");
    }
    else if (kind == 9)
    {
        check_lanes_test_append(text, capacity, used, "    long r = sum_values(3, a, b, a + b) + sum_values(1, b);\n");
    }
    else if (kind == 10)
    {
        check_lanes_test_append(text, capacity, used,
                                "    static long counter = %u;\n    counter += a & 3;\n    static const char text[] = \"lanes %u\";\n"
                                "    long r = counter + text[(unsigned long)b %% (sizeof(text) - 1)];\n", index, index);
    }
    else
    {
        check_lanes_test_append(text, capacity, used, "    long r = a;\n    while (r > 1000)\n    {\n        r = r / 2 + b %% 3;\n    }\n");
    }
    if (callee != UINT32_MAX)
    {
        check_lanes_test_append(text, capacity, used, "    r ^= f%u(a + 1, b);\n", callee);
    }
    check_lanes_test_append(text, capacity, used, "    return r;\n}\n");
}

// A generated program of `functions` functions of random kinds (see the file's comment), each calling at most one before
// it, and a main that calls each with a few inputs and exits with a checksum of their results.
static uint64_t check_lanes_test_generate(CheckLanesTestTally* tally, char* text, uint64_t capacity, uint32_t functions)
{
    uint64_t used = 0;
    check_lanes_test_append(text, capacity, &used,
                            "#include <stdarg.h>\n"
                            "struct pair { long x; long y; };\n"
                            "static struct pair swap_pair(struct pair p)\n{\n    struct pair q = { p.y, p.x };\n    return q;\n}\n"
                            "static long sum_values(int count, ...)\n{\n    va_list list;\n    va_start(list, count);\n    long total = 0;\n"
                            "    for (int i = 0; i < count; i += 1)\n    {\n        total += va_arg(list, long);\n    }\n    va_end(list);\n    return total;\n}\n");
    for (uint32_t f = 0; f < functions; f += 1)
    {
        const uint32_t kind = (uint32_t)(check_lanes_test_random(tally) % BUSTER_CHECK_LANES_TEST_KINDS);
        const uint32_t callee = f != 0 && check_lanes_test_random(tally) % 3 == 0 ? (uint32_t)(check_lanes_test_random(tally) % f) : UINT32_MAX;
        check_lanes_test_function(text, capacity, &used, f, kind, callee);
    }
    check_lanes_test_append(text, capacity, &used, "int main(void)\n{\n    long total = 0;\n    for (long i = 0; i < 3; i += 1)\n    {\n");
    for (uint32_t f = 0; f < functions; f += 1)
    {
        check_lanes_test_append(text, capacity, &used, "        total = total * 31 + f%u(i, total & 255);\n", f);
    }
    check_lanes_test_append(text, capacity, &used, "    }\n    return (int)(total & 127);\n}\n");
    return used < capacity ? used : capacity;
}

// One compilation through the pipeline to its selected program (the driver's steps before check_program_emit), which
// every emission of the tests reads: everything it points at lives here.
typedef struct CheckLanesTestUnit CheckLanesTestUnit;
struct CheckLanesTestUnit
{
    CheckInput input;
    CheckStream stream;
    CheckInitializers initializers;
    CheckAsms asms;
    CheckDataPrepared data;
    CheckProgram program;
    CheckProgramSelection selected;
    Arena arena;
    Arena output;
    Arena scratch;
    Arena type_arena;
    Arena index_arena;
    TypeTable types;
    bool ready;
};

static void check_lanes_test_close(CheckLanesTestUnit* unit)
{
    arena_release(&unit->index_arena);
    arena_release(&unit->type_arena);
    arena_release(&unit->scratch);
    arena_release(&unit->output);
    arena_release(&unit->arena);
}

// Compiles `path` to its selected program into `unit` by the scalar path or the vector one, with `pp` and `parser`,
// which the caller keeps and releases; when `ordered`, with its identifiers' ids in another order, as the driver's
// --id-order `id_order` gives them (check_driver_id_order); with `tier1`, lowered for Tier 1 as the driver's --tier1 lowers
// (CheckProgramOptions.asm_bodies: its asm statements' helpers and bodies are Tier 1's).
static void check_lanes_test_compile_order(CheckLanesTestUnit* unit, Preprocessor* pp, Parser* parser, InternTable* identifiers, const char* path, bool scalar,
                                           bool ordered, uint64_t id_order, bool tier1)
{
    *pp = (Preprocessor){ 0 };
    *parser = (Parser){ 0 };
    *identifiers = (InternTable){ 0 };
    *unit = (CheckLanesTestUnit){
        .arena = arena_reserve(UINT64_C(1) << 32),
        .output = arena_reserve(UINT64_C(1) << 32),
        .scratch = arena_reserve(UINT64_C(1) << 32),
    };
    char* arguments[] = { (char*)"-march=x86-64", (char*)path };
    PPOptions options;
    const char* input = 0;
    const char* output = 0;
    bool markers = false;
    bool ready = unit->arena.base != 0 && unit->output.base != 0 && unit->scratch.base != 0 &&
                 pp_parse_options(BUSTER_ARRAY_LENGTH(arguments), arguments, &options, &unit->arena, &input, &output, &markers);
    options.semantic_pragmas = true;
    options.quiet = true;
    const ParseOptions parse_options = { .scalar = scalar };
    ready = ready && intern_initialize(identifiers) && (!ordered || check_driver_id_order(identifiers, &options, input, id_order)) &&
            pp_initialize(pp, &options, identifiers) && parse_initialize(parser, &parse_options, options.language) && pp_run(pp, input) && pp->errors == 0 &&
            parse_translation_unit(parser, pp) && parser->errors == 0;
    const uint32_t tokens = ready ? parser->tokens.count : 0;
    const TypeCapacity capacity = {
        .types = tokens * 4 + 64,
        .parameters = tokens,
        .tags = tokens,
        .members = tokens,
    };
    if (ready)
    {
        unit->type_arena = arena_reserve(type_capacity_bytes(capacity));
        unit->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
        ready = type_initialize(&unit->types, &unit->type_arena, &unit->index_arena, capacity);
    }
    const CheckOptions check_options = { .scalar = scalar };
    ready = ready && check_input(&unit->input, parser, pp, &unit->types, &unit->arena, &unit->scratch) &&
            check_graph_run(&unit->types, &unit->input.graph, &unit->output, &unit->scratch, &check_options, &unit->stream) && unit->stream.report_count == 0 &&
            check_asms_run(&unit->asms, &unit->input, &unit->stream, &unit->types, &unit->output, &unit->scratch) &&
            check_initializers_run(&unit->initializers, &unit->input, &unit->stream, parser, &unit->types, &unit->output, &unit->scratch) &&
            check_data_prepare(&unit->data, &unit->input, &unit->stream, &unit->initializers, &unit->types, scalar, &unit->output, &unit->scratch);
    const CheckProgramOptions program_options = {
        .initializers = &unit->initializers,
        .asms = &unit->asms,
        .identifiers = identifiers,
        .initializer_references = unit->data.references,
        .initializer_reference_count = unit->data.reference_count,
        .scalar = scalar,
        .asm_bodies = tier1,
        .target = options.target,
        .predefines = &options.predefines,
    };
    ready = ready && check_program(&unit->program, parser, &unit->input, &unit->stream, &unit->types, program_options, &unit->output, &unit->scratch) &&
            check_program_select(&unit->selected, &unit->program, &unit->data, scalar, &unit->output, &unit->scratch);
    unit->ready = ready;
}

static void check_lanes_test_compile(CheckLanesTestUnit* unit, Preprocessor* pp, Parser* parser, InternTable* identifiers, const char* path, bool scalar)
{
    check_lanes_test_compile_order(unit, pp, parser, identifiers, path, scalar, false, 0, false);
}

// The same, lowered for Tier 1 (check_lanes_test_compile_order's `tier1`).
static __attribute__((unused)) void check_lanes_test_compile_tier1(CheckLanesTestUnit* unit, Preprocessor* pp, Parser* parser, InternTable* identifiers, const char* path,
                                                                   bool scalar)
{
    check_lanes_test_compile_order(unit, pp, parser, identifiers, path, scalar, false, 0, true);
}

// One emission of a compiled unit and what it made: its object's bytes (malloc'd, the caller frees them), or its
// failure, and the lanes and parts it ran, the lane that took a squeezed part, and why it ran again on one lane.
typedef struct CheckLanesTestEmission CheckLanesTestEmission;
struct CheckLanesTestEmission
{
    uint8_t* object;
    uint64_t size;
    bool emitted;
    bool built;
    CheckProgramStatus status;
    uint32_t token;
    uint32_t backend_node;
    Tier0Error backend_error;
    CheckTier1Outcome tier1_failure;
    bool rolled_back;
    const char* reason;
    uint32_t active;
    uint32_t parts;
    uint32_t squeezed;
    CheckProgramAgain again;
    // Whether the emission ran in the address space it was limited to (check_lanes_test_emit_limited), and with lanes,
    // their first arenas' scratch, lane 0's writer's columns, as it left it: its capacity, 0 where it couldn't be
    // reserved and the columns went in the caller's scratch, and its length.
    bool limited;
    uint64_t columns_capacity;
    uint64_t columns_length;
};

// The address space the process has mapped (/proc/self/statm's first field, the kernel's total that RLIMIT_AS bounds),
// or 0 if it can't be read.
static __attribute__((unused)) uint64_t check_lanes_test_mapped(void)
{
    char text[128] = { 0 };
    const int file = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    const ssize_t length = file >= 0 ? read(file, text, sizeof(text) - 1) : -1;
    if (file >= 0)
    {
        close(file);
    }
    return length > 0 ? strtoull(text, 0, 10) * (uint64_t)sysconf(_SC_PAGESIZE) : 0;
}

// check_lanes_test_emit, with the emission alone limited (RLIMIT_AS) to `room` bytes of address space more than the
// process has, and to `scratch_room` bytes of its scratch arena, UINT64_MAX for neither: the test's child's
// (check_lanes_test_address_space).
static CheckLanesTestEmission check_lanes_test_emit_limited(CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp, const InternTable* identifiers,
                                                            CheckProgramLanes* lanes, bool scalar, bool rows, bool unwind, uint64_t room, uint64_t scratch_room)
{
    CheckLanesTestEmission result = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 32);
    Arena bytes = arena_reserve(UINT64_C(1) << 32);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    CheckProgramCode code = { 0 };
    CheckProgramRows row_set = { 0 };
    CheckDebug dwarf = { 0 };
    const CheckProgram* program = &unit->selected.program;
    const bool ready = output.base != 0 && bytes.base != 0 && scratch.base != 0;
#if BUSTER_SANITIZER_RESOURCES
    const bool before_limited = lane_test_resources.arena_limited;
    const uint64_t before_room = lane_test_resources.arena_limit;
    lane_test_resources.arena_limited = room != UINT64_MAX;
    lane_test_resources.arena_limit = room;
    result.limited = room != UINT64_MAX;
#else
    struct rlimit before = { 0 };
    const bool got = ready && room != UINT64_MAX && getrlimit(RLIMIT_AS, &before) == 0;
    const uint64_t mapped = got ? check_lanes_test_mapped() : 0;
    const struct rlimit limit = {
        .rlim_cur = mapped + room < before.rlim_max ? mapped + room : before.rlim_max,
        .rlim_max = before.rlim_max,
    };
    result.limited = mapped != 0 && setrlimit(RLIMIT_AS, &limit) == 0;
#endif
    const uint64_t scratch_capacity = scratch.capacity;
    scratch.capacity = scratch_room < scratch_capacity ? scratch_room : scratch_capacity;
    const bool emitted = ready && (room == UINT64_MAX || result.limited) &&
                         (lanes != NULL ? check_program_emit_lanes(&code, program, parser, identifiers, unit->selected.data.capacity, scalar, &output, &bytes, &scratch,
                                                                   NULL, rows ? &row_set : NULL, unwind, true, lanes) :
                                          check_program_emit(&code, program, parser, identifiers, unit->selected.data.capacity, scalar, &output, &bytes, &scratch, NULL,
                                                             rows ? &row_set : NULL, unwind, true));
#if BUSTER_SANITIZER_RESOURCES
    lane_test_resources.arena_limited = before_limited;
    lane_test_resources.arena_limit = before_room;
#else
    result.limited = result.limited && setrlimit(RLIMIT_AS, &before) == 0;
#endif
    scratch.capacity = scratch_capacity;
    result.columns_capacity = lanes != NULL ? lanes->arenas[0].scratch.capacity : 0;
    result.columns_length = lanes != NULL ? lanes->arenas[0].scratch.length : 0;
    result.emitted = emitted;
    result.status = code.status;
    result.token = code.token;
    result.backend_node = code.backend_node;
    result.backend_error = code.backend_error;
    result.tier1_failure = code.tier1_failure;
    result.rolled_back = !emitted && output.length == 0 && bytes.length == 0 && scratch.length == 0 && code.link.atoms.count == 0 &&
                         code.link.symbols.count == 0 && code.records == 0 && code.function_atoms == NULL;
    result.reason = code.reason;
    result.active = lanes != NULL ? lanes->active : 1;
    result.parts = lanes != NULL ? lanes->parts : 1;
    result.squeezed = lanes != NULL ? lanes->squeezed : 0;
    result.again = lanes != NULL ? lanes->again : CHECK_PROGRAM_AGAIN_NONE;
    bool built = emitted && (!rows || check_debug_build(&dwarf, program, &row_set, parser, pp, scalar, &output, &scratch));
    CheckDataResult published = { 0 };
    built = built && check_data_publish(&unit->selected.data, &unit->types, program->stream, program->object_symbols, program->object_count, &code.link, 0, &bytes,
                                        &scratch, &published);
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    built = built && link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, &scratch) && object_map_symbols(&symbols, &code.link, &scratch);
    if (built)
    {
        const ObjectOptions options = { .debug = rows ? &dwarf.object : NULL, .debug_count = rows ? 1 : 0 };
        const Object object = object_build(&code.link, &units, &symbols, 0, options, (String8){ .pointer = "lanes.c", .length = 7 }, &scratch);
        built = object.error == OBJECT_ERROR_NONE && (result.object = malloc(object.size)) != NULL;
        if (built)
        {
            memcpy(result.object, object.bytes, object.size);
            result.size = object.size;
        }
    }
    result.built = built;
    check_program_lanes_release(lanes);
    arena_release(&scratch);
    arena_release(&bytes);
    arena_release(&output);
    return result;
}

// Emits `unit`'s selected program on `lanes` (NULL for check_program_emit's one), with -g's rows (`rows`) and unwind
// tables (`unwind`), then publishes its static data and builds its object, as check_driver_object does.
static CheckLanesTestEmission check_lanes_test_emit(CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp, const InternTable* identifiers,
                                                    CheckProgramLanes* lanes, bool scalar, bool rows, bool unwind)
{
    return check_lanes_test_emit_limited(unit, parser, pp, identifiers, lanes, scalar, rows, unwind, UINT64_MAX, UINT64_MAX);
}

// Whether two emissions made the same: the same object, or the same failure.
static bool check_lanes_test_same(const CheckLanesTestEmission* a, const CheckLanesTestEmission* b)
{
    bool same = a->emitted == b->emitted && a->built == b->built && a->status == b->status && a->token == b->token && a->backend_node == b->backend_node &&
                a->backend_error == b->backend_error && ((a->reason == NULL) == (b->reason == NULL)) &&
                (a->reason == NULL || strcmp(a->reason, b->reason) == 0) && a->size == b->size;
    return same && (a->size == 0 || memcmp(a->object, b->object, a->size) == 0);
}

#include "c_check_backend_startup_test.c"
// The real emission consumes external slots and returns them before its normal, failed-start or resource retry
// completes. Its independent object/failure oracle is the ordinary one-lane emission above; the pipe contents are
// the ownership oracle, including the exact exceptional bytes and no implicit calling-thread token.
static void check_lanes_test_budget(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp,
                                     const InternTable* identifiers, bool scalar, bool rows, bool unwind, const CheckLanesTestEmission* one)
{
    const uint8_t tokens[] = { 0, 73, 255 };
    uint32_t unusable[BUSTER_CHECK_EMIT_MAX_LANES];
    for (uint32_t lane = 0; lane < BUSTER_CHECK_EMIT_MAX_LANES; lane += 1)
    {
        unusable[lane] = BUSTER_LANE_CPU_MASK_WORDS * 64;
    }
    for (uint32_t retry = 0; retry < 3; retry += 1)
    {
        int fds[2] = { -1, -1 };
        const bool ready = pipe(fds) == 0 && write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens);
        check_lanes_test_expect(tally, ready, "external ownership fixture has its three exact tokens", NULL);
        if (ready)
        {
            char flags[128];
            snprintf(flags, sizeof(flags), "--jobserver-auth=%d,%d", fds[0], fds[1]);
            WorkerBudget budget;
            worker_budget_initialize(&budget, flags, 8);
            CheckProgramLanes lanes = {
                .lanes = 8,
                .every_lane = true,
                .cpus = retry == 1 ? unusable : NULL,
                .budget = &budget,
                .squeeze_body = retry == 2 ? 1 : 0,
                .squeeze_arenas = retry == 2 ? BUSTER_CHECK_EMIT_SQUEEZE_BYTES : 0,
            };
            CheckLanesTestEmission many = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, rows, unwind);
            const uint32_t bodies = unit->selected.program.body_count;
            const uint32_t expected = bodies != 0 && bodies < 4 ? bodies : bodies != 0 ? 4 : 1;
            check_lanes_test_expect(tally, check_lanes_test_same(one, &many),
                                    "external ownership and every retry preserve the one-lane object or failure", NULL);
            check_lanes_test_expect(tally, many.active == expected && !budget.return_failed &&
                                    worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)),
                                    "emission returns every exact slot on ordinary, failed-start and resource paths", NULL);
            if (retry == 1 && expected > 1)
            {
                check_lanes_test_expect(tally, many.again == CHECK_PROGRAM_AGAIN_START, "external ownership is released before failed-start retry", NULL);
            }
            if (retry == 2 && expected > 1 && one->emitted)
            {
                check_lanes_test_expect(tally, many.again != CHECK_PROGRAM_AGAIN_NONE, "external ownership is released before resource retry", NULL);
            }
            free(many.object);
            worker_budget_close(&budget);
        }
        if (fds[0] >= 0)
        {
            close(fds[0]);
        }
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
    }
}

// `unit` emitted on one lane, then on 2, 3 and 8 lanes, every lane forced, on 8 with lanes 0, 1 and 7 each slow in
// turn, and on 8 that can't be pinned, with and without -g's rows and unwind tables: each emission the one lane's, the
// unpinned one run again on one lane, the others not. Returns how many emissions it compared, and `emitted` whether the
// one lane's succeeded.
static uint32_t check_lanes_test_unit(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp,
                                      const InternTable* identifiers, bool scalar, const char* what, bool* emitted)
{
    static const uint32_t counts[] = { 2, 3, 8 };
    static const uint32_t slow[] = { 1, 2, 8 };
    // CPUs past every one lane_pin takes: lane_run runs the lanes unpinned, then says they didn't start.
    uint32_t unusable[BUSTER_CHECK_EMIT_MAX_LANES];
    for (uint32_t lane = 0; lane < BUSTER_CHECK_EMIT_MAX_LANES; lane += 1)
    {
        unusable[lane] = BUSTER_LANE_CPU_MASK_WORDS * 64;
    }
    uint32_t compared = 0;
    char detail[256];
    *emitted = false;
    for (uint32_t shape = 0; shape < 2; shape += 1)
    {
        const bool rows = shape == 1;
        const bool unwind = shape == 1;
        CheckLanesTestEmission one = check_lanes_test_emit(unit, parser, pp, identifiers, NULL, scalar, rows, unwind);
        *emitted |= one.emitted;
        check_lanes_test_budget(tally, unit, parser, pp, identifiers, scalar, rows, unwind, &one);
        for (uint32_t run = 0; run <= BUSTER_ARRAY_LENGTH(counts) + BUSTER_ARRAY_LENGTH(slow); run += 1)
        {
            const bool slowed = run >= BUSTER_ARRAY_LENGTH(counts) && run < BUSTER_ARRAY_LENGTH(counts) + BUSTER_ARRAY_LENGTH(slow);
            const bool unpinned = run == BUSTER_ARRAY_LENGTH(counts) + BUSTER_ARRAY_LENGTH(slow);
            CheckProgramLanes lanes = {
                .lanes = slowed || unpinned ? 8 : counts[run],
                .every_lane = true,
                .cpus = unpinned ? unusable : 0,
                .slow_lane = slowed ? slow[run - BUSTER_ARRAY_LENGTH(counts)] : 0,
                .slow_microseconds = 500,
            };
            CheckLanesTestEmission many = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, rows, unwind);
            const uint32_t bodies = unit->selected.program.body_count;
            const uint32_t expected = lanes.lanes < bodies ? lanes.lanes : bodies != 0 ? bodies : 1;
            snprintf(detail, sizeof(detail), "%s, %s path, %u lanes%s%s: status %u/%u, token %u/%u, %llu/%llu bytes, %u lanes ran, again %u", what,
                     scalar ? "scalar" : "vector", lanes.lanes, slowed ? " with a slow one" : unpinned ? " that can't be pinned" : "",
                     rows ? ", -g and unwind tables" : "", (uint32_t)one.status, (uint32_t)many.status, one.token, many.token, (unsigned long long)one.size,
                     (unsigned long long)many.size, many.active, (uint32_t)many.again);
            check_lanes_test_expect(tally, check_lanes_test_same(&one, &many), "an emission on lanes makes one lane's", detail);
            const CheckProgramAgain again = unpinned && expected > 1 ? CHECK_PROGRAM_AGAIN_START : CHECK_PROGRAM_AGAIN_NONE;
            check_lanes_test_expect(tally, many.active == expected && many.again == again && (expected == 1 || again != CHECK_PROGRAM_AGAIN_NONE || many.parts > 1),
                                    "the emission runs every lane it may, again on one only when they don't start", detail);
            free(many.object);
            compared += 1;
        }
        free(one.object);
    }
    return compared;
}

// A failed setup of the priming preprocessor must fail the requested property check. SOURCE_DATE_EPOCH is validated
// during that setup; a child supplies an invalid value without changing this process's environment.
static bool check_lanes_test_order_setup(void)
{
    const pid_t child = fork();
    if (child == 0)
    {
        InternTable identifiers = { 0 };
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const bool initialized = setenv("SOURCE_DATE_EPOCH", "invalid", 1) == 0 && intern_initialize(&identifiers);
        const bool refused = initialized && !check_driver_id_order(&identifiers, &options, "unused.c", 153);
        intern_release(&identifiers);
        _exit(refused ? 0 : 1);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The private preparation must not borrow a caller's table-bound cache, and a source error in that preparation stays
// for the real compile to report: the names preceding it still prime the requested order.
static bool check_lanes_test_order_cache(const char* directory)
{
    static const char source[] = "int id_order_cached_name0;int id_order_cached_name1;\n#error expected\n";
    char path[PATH_MAX];
    const int length = snprintf(path, sizeof(path), "%s/id-order.c", directory);
    FILE* file = length > 0 && length < (int)sizeof(path) ? fopen(path, "wb") : NULL;
    bool written = file != NULL;
    if (file != NULL)
    {
        written = fwrite(source, 1, sizeof(source) - 1, file) == sizeof(source) - 1;
        written = fclose(file) == 0 && written;
    }
    InternTable identifiers = { 0 };
    FileCache cache = { 0 };
    PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool initialized = written && intern_initialize(&identifiers) && file_cache_initialize(&cache, &identifiers, pp_prepare_options(&options));
    const FileCache before = cache;
    options.files = &cache;
    const bool result = initialized && check_driver_id_order(&identifiers, &options, path, 153) &&
                        (intern_find(&identifiers, (const uint8_t*)"id_order_cached_name0", sizeof("id_order_cached_name0") - 1) != 0 ||
                         intern_find(&identifiers, (const uint8_t*)"id_order_cached_name1", sizeof("id_order_cached_name1") - 1) != 0) &&
                        memcmp(&cache, &before, sizeof(cache)) == 0;
    file_cache_release(&cache);
    intern_release(&identifiers);
    return result;
}

// `unit`, compiled from `path` with `identifiers`, emitted on one lane, then compiled again with its identifiers' ids in
// another order (check_lanes_test_compile_order with `id_order`) and emitted on one lane and on 8, every lane forced,
// with -g's rows and unwind tables and without: each emission the first one's object or failure (#153). The second
// compile's table must have every name of the first, many of them at other ids. Returns how many emissions it compared.
static uint32_t check_lanes_test_order(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp,
                                       const InternTable* identifiers, const char* path, bool scalar, uint64_t id_order, const char* what)
{
    static InternTable ordered_identifiers;
    static Preprocessor ordered_pp;
    static Parser ordered_parser;
    CheckLanesTestUnit ordered;
    check_lanes_test_compile_order(&ordered, &ordered_pp, &ordered_parser, &ordered_identifiers, path, scalar, true, id_order, false);
    char detail[256];
    snprintf(detail, sizeof(detail), "%s, %s path, --id-order %llu", what, scalar ? "scalar" : "vector", (unsigned long long)id_order);
    check_lanes_test_expect(tally, ordered.ready, "compile a program with its identifiers' ids in another order", detail);
    uint32_t found = 0;
    uint32_t moved = 0;
    for (IdentId id = C_NAME_COUNT; ordered.ready && id < identifiers->count; id += 1)
    {
        const IdentId other = intern_find(&ordered_identifiers, intern_bytes(identifiers, id), intern_length(identifiers, id));
        found += other != 0;
        moved += other != 0 && other != id;
    }
    const uint32_t names = identifiers->count - C_NAME_COUNT;
    check_lanes_test_expect(tally, !ordered.ready || (found == names && moved * 2 >= names), "the second compile has every name, most at other ids", detail);
    uint32_t compared = 0;
    for (uint32_t shape = 0; ordered.ready && shape < 2; shape += 1)
    {
        const bool rows = shape == 1;
        CheckLanesTestEmission one = check_lanes_test_emit(unit, parser, pp, identifiers, NULL, scalar, rows, rows);
        for (uint32_t run = 0; run < 2; run += 1)
        {
            CheckProgramLanes lanes = {
                .lanes = 8,
                .every_lane = true,
            };
            CheckLanesTestEmission other = check_lanes_test_emit(&ordered, &ordered_parser, &ordered_pp, &ordered_identifiers, run == 0 ? NULL : &lanes, scalar, rows,
                                                                 rows);
            char compare[384];
            snprintf(compare, sizeof(compare), "%s, %s%s: status %u/%u, token %u/%u, %llu/%llu bytes", detail, run == 0 ? "one lane" : "8 lanes",
                     rows ? ", -g and unwind tables" : "", (uint32_t)one.status, (uint32_t)other.status, one.token, other.token, (unsigned long long)one.size,
                     (unsigned long long)other.size);
            check_lanes_test_expect(tally, check_lanes_test_same(&one, &other), "an emission with the ids in another order makes the same object or failure",
                                    compare);
            free(other.object);
            compared += 1;
        }
        free(one.object);
    }
    check_lanes_test_close(&ordered);
    parse_release(&ordered_parser);
    pp_release(&ordered_pp);
    intern_release(&ordered_identifiers);
    return compared;
}

// Generated programs (see the file's comment), written to `directory` as program_N.c, compiled by each path and emitted
// on every set of lanes, and again with their identifiers' ids in another order, from `id_order` on.
static void check_lanes_test_programs(CheckLanesTestTally* tally, const char* directory, uint32_t programs, uint64_t id_order)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const uint64_t capacity = UINT64_C(1) << 24;
    char* text = malloc(capacity);
    uint64_t emissions = 0;
    uint64_t ordered = 0;
    uint64_t functions_total = 0;
    for (uint32_t p = 0; text != NULL && p < programs + 2; p += 1)
    {
        const uint32_t functions = p >= programs ? 600 : p < 3 ? 1 + p : 1 + (uint32_t)(check_lanes_test_random(tally) % 240);
        const uint64_t length = check_lanes_test_generate(tally, text, capacity, functions);
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/program_%u.c", directory, p);
        FILE* file = fopen(path, "wb");
        const bool written = file != NULL && fwrite(text, 1, length, file) == length;
        const bool closed = file != NULL && fclose(file) == 0;
        check_lanes_test_expect(tally, written && closed, "write a generated program", path);
        for (uint32_t scalar = 0; written && closed && scalar < 2; scalar += 1)
        {
            CheckLanesTestUnit unit;
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
            check_lanes_test_expect(tally, unit.ready, "compile a generated program to its selected program", path);
            bool emitted = false;
            emissions += unit.ready ? check_lanes_test_unit(tally, &unit, &parser, &pp, &identifiers, scalar != 0, path, &emitted) : 0;
            ordered += unit.ready ? check_lanes_test_order(tally, &unit, &parser, &pp, &identifiers, path, scalar != 0, id_order + p, path) : 0;
            check_lanes_test_expect(tally, !unit.ready || emitted, "a generated program emits", path);
            functions_total += unit.ready ? unit.selected.program.body_count : 0;
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
    }
    free(text);
    printf("  programs: %u generated, %llu functions compiled, %llu emissions on lanes and %llu with the ids in another order, each one lane's\n", programs + 2,
           (unsigned long long)functions_total, (unsigned long long)emissions, (unsigned long long)ordered);
}

// A failing program: its source and the step the first failure is at, which every lane count must report.
typedef struct CheckLanesTestFailure CheckLanesTestFailure;
struct CheckLanesTestFailure
{
    const char* name;
    const char* source;
    CheckProgramStatus status;
    Tier0Error backend_error;
};

// Bodies that fail at different steps, in both orders: lowering refuses a wide _BitInt bit-field; Tier 0 refuses
// 256 live masks, whose separate byte-depth bound remains (#179). Ordinary deep expressions no longer provide a
// refusal (#1106). Source-order lowering takes every body before Tier 0 takes any, so it fails first. The explicit
// target enables mask-class lowering even in baseline-host tests; no emitted mask instructions are executed here.
#define BUSTER_CHECK_LANES_TEST_MASK_DEPTH \
    "#define K1(v) __builtin_ia32_kandhi((unsigned short)a, v)\n" \
    "#define K4(v) K1(K1(K1(K1(v))))\n" \
    "#define K16(v) K4(K4(K4(K4(v))))\n" \
    "#define K64(v) K16(K16(K16(K16(v))))\n" \
    "#define K256(v) K64(K64(K64(K64(v))))\n" \
    "__attribute__((target(\"avx512f,avx512vl,avx512bw,avx512dq\"))) int deep(int a) { return K256((unsigned short)a); }\n"
static const CheckLanesTestFailure check_lanes_test_failures[] = {
    {
        "lowering after Tier 0",
        "int fine0(int a) { return a + 1; }\n"
        BUSTER_CHECK_LANES_TEST_MASK_DEPTH
        "int fine1(int a) { return a * 2; }\n"
        "int refused(int a) { struct { unsigned _BitInt(200) x : 150; } w; w.x = a; return (int)w.x; }\n"
        "int main(void) { return fine0(1) + deep(2) + fine1(3) + refused(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
        TIER0_OK,
    },
    {
        "lowering before Tier 0",
        "int refused(int a) { struct { unsigned _BitInt(200) x : 150; } w; w.x = a; return (int)w.x; }\n"
        "int fine0(int a) { return a + 1; }\n"
        BUSTER_CHECK_LANES_TEST_MASK_DEPTH
        "int main(void) { return fine0(1) + deep(2) + refused(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
        TIER0_OK,
    },
    {
        "two lowering refusals",
        "int fine0(int a) { return a + 1; }\n"
        "int refused0(int a) { struct { unsigned _BitInt(200) x : 150; } w; w.x = a; return (int)w.x; }\n"
        "int fine1(int a) { return a * 2; }\n"
        "int refused1(int a) { struct { unsigned _BitInt(300) y : 250; } v; v.y = a; return (int)v.y; }\n"
        "int main(void) { return fine0(1) + refused0(2) + fine1(3) + refused1(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
        TIER0_OK,
    },
    {
        "Tier 0 alone",
        "int fine0(int a) { return a + 1; }\n"
        BUSTER_CHECK_LANES_TEST_MASK_DEPTH
        "int fine1(int a) { return a * 2; }\n"
        "int main(void) { return fine0(1) + deep(2) + fine1(3); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
        TIER0_ERROR_DEPTH,
    },
};

// Each failing program (see the file's comment) emitted on every set of lanes, and with its identifiers' ids in another
// order, from `id_order` on: one lane's failure, at its step.
static void check_lanes_test_failing(CheckLanesTestTally* tally, const char* directory, uint64_t id_order)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    char detail[256];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_lanes_test_failures); f += 1)
    {
        const CheckLanesTestFailure* failure = check_lanes_test_failures + f;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/failure_%u.c", directory, f);
        FILE* file = fopen(path, "wb");
        const uint64_t length = strlen(failure->source);
        const bool written = file != NULL && fwrite(failure->source, 1, length, file) == length;
        const bool closed = file != NULL && fclose(file) == 0;
        check_lanes_test_expect(tally, written && closed, "write a failing program", failure->name);
        for (uint32_t scalar = 0; written && closed && scalar < 2; scalar += 1)
        {
            CheckLanesTestUnit unit;
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
            check_lanes_test_expect(tally, unit.ready, "compile a failing program to its selected program", failure->name);
            bool emitted = true;
            if (unit.ready)
            {
                CheckLanesTestEmission one = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, scalar != 0, false, true);
                snprintf(detail, sizeof(detail), "%s: status %u, backend %u, reason %s", failure->name, (uint32_t)one.status, (uint32_t)one.backend_error,
                         one.reason != NULL ? one.reason : "none");
                check_lanes_test_expect(tally, !one.emitted && one.status == failure->status && one.backend_error == failure->backend_error,
                                        "a failing program fails at its step on one lane", detail);
                free(one.object);
                check_lanes_test_unit(tally, &unit, &parser, &pp, &identifiers, scalar != 0, failure->name, &emitted);
                check_lanes_test_order(tally, &unit, &parser, &pp, &identifiers, path, scalar != 0, id_order + f, failure->name);
            }
            check_lanes_test_expect(tally, !emitted, "a failing program fails on every set of lanes", failure->name);
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
    }
}

// The squeezes' generated program: its seed, whatever --seed is, so that the reasons its sweeps reach stay the same, and
// its functions.
#define BUSTER_CHECK_LANES_TEST_SQUEEZE_SEED 0x151151151151151ull
#define BUSTER_CHECK_LANES_TEST_SQUEEZE_FUNCTIONS 24
// The most room a squeeze leaves: any part of the tests' programs fits in it.
#define BUSTER_CHECK_LANES_TEST_MAX_ROOM (UINT64_C(1) << 30)

// What the squeezes ran: their emissions, how many of them ran again on one lane for each reason (by
// CheckProgramAgain), and how many squeezed parts that didn't fit lane 0 and the other lanes took.
typedef struct CheckLanesTestSqueezes CheckLanesTestSqueezes;
struct CheckLanesTestSqueezes
{
    uint64_t emissions;
    uint64_t again[CHECK_PROGRAM_AGAIN_CODE + 1];
    uint64_t first_lane;
    uint64_t other_lanes;
};

// `unit` on 8 lanes with a lane short of room (see the file's comment): its bodies with the fewest and the most tokens,
// whose parts the lanes are likely to take last and first, each squeezed in turn in the output, scratch and bytes arenas
// of the lane that takes its part, from no room to spare up, doubling, until the part fits. One lane's emission must make
// its object where the program `succeeds`, and fail where it doesn't; each emission on lanes must be one lane's, run
// again on one lane for a resource's reason while the part doesn't fit.
static void check_lanes_test_squeeze(CheckLanesTestTally* tally, CheckLanesTestSqueezes* squeezes, CheckLanesTestUnit* unit, const Parser* parser,
                                     Preprocessor* pp, const InternTable* identifiers, bool scalar, bool rows, bool succeeds, const char* what)
{
    static const uint32_t arenas[] = { BUSTER_CHECK_EMIT_SQUEEZE_OUTPUT, BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH, BUSTER_CHECK_EMIT_SQUEEZE_BYTES };
    static const char* const names[] = { "output", "scratch", "bytes" };
    const CheckProgram* program = &unit->selected.program;
    uint32_t bodies[2] = { 0, 0 };
    uint64_t fewest = UINT64_MAX;
    uint64_t most = 0;
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const uint64_t tokens = (uint64_t)parser->tree.parents[program->bodies[b].opening] - program->bodies[b].opening + 1;
        bodies[0] = tokens < fewest ? b : bodies[0];
        fewest = tokens < fewest ? tokens : fewest;
        bodies[1] = tokens > most ? b : bodies[1];
        most = tokens > most ? tokens : most;
    }
    char detail[384];
    CheckLanesTestEmission one = check_lanes_test_emit(unit, parser, pp, identifiers, NULL, scalar, rows, rows);
    snprintf(detail, sizeof(detail), "%s, %s path%s: status %u, reason %s", what, scalar ? "scalar" : "vector", rows ? ", -g and unwind tables" : "", (uint32_t)one.status,
             one.reason != NULL ? one.reason : "none");
    check_lanes_test_expect(tally, succeeds ? one.emitted && one.built : !one.emitted,
                            succeeds ? "the squeezes' program makes its object on one lane" : "a squeezed failing program fails on one lane", detail);
    for (uint32_t s = 0; program->body_count > 1 && s < BUSTER_ARRAY_LENGTH(bodies); s += 1)
    {
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(arenas); a += 1)
        {
            bool fits = false;
            uint64_t room = 0;
            while (!fits && room <= BUSTER_CHECK_LANES_TEST_MAX_ROOM)
            {
                CheckProgramLanes lanes = {
                    .lanes = 8,
                    .every_lane = true,
                    .squeeze_body = bodies[s] + 1,
                    .squeeze_arenas = arenas[a],
                    .squeeze_room = room,
                };
                CheckLanesTestEmission many = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, rows, rows);
                fits = many.again == CHECK_PROGRAM_AGAIN_NONE;
                snprintf(detail, sizeof(detail),
                         "%s, %s path%s, body %u with %llu bytes to spare in its lane's %s arena: status %u/%u, token %u/%u, %llu/%llu bytes, lane %u took it, again %u",
                         what, scalar ? "scalar" : "vector", rows ? ", -g and unwind tables" : "", bodies[s], (unsigned long long)room, names[a], (uint32_t)one.status,
                         (uint32_t)many.status, one.token, many.token, (unsigned long long)one.size, (unsigned long long)many.size, many.squeezed, (uint32_t)many.again);
                check_lanes_test_expect(tally, check_lanes_test_same(&one, &many), "an emission whose lane runs out of room makes one lane's", detail);
                check_lanes_test_expect(tally, many.squeezed != 0 && many.again != CHECK_PROGRAM_AGAIN_START,
                                        "a lane takes the squeezed part, and runs out of room rather than not start", detail);
                squeezes->emissions += 1;
                squeezes->again[many.again] += 1;
                squeezes->first_lane += !fits && many.squeezed == 1 ? 1 : 0;
                squeezes->other_lanes += !fits && many.squeezed > 1 ? 1 : 0;
                free(many.object);
                room = room == 0 ? 64 : room * 2;
            }
            check_lanes_test_expect(tally, fits, "a squeezed part fits with room enough", detail);
        }
    }
    free(one.object);
}

// The squeezes (see the file's comment): a generated program of its own seed, written to `directory` as squeeze.c, and
// each failing program (failure_N.c), by each path, with -g's rows and unwind tables and without.
static void check_lanes_test_squeezes(CheckLanesTestTally* tally, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    CheckLanesTestSqueezes generated = { 0 };
    CheckLanesTestSqueezes failing = { 0 };
    const uint64_t capacity = UINT64_C(1) << 24;
    char* text = malloc(capacity);
    CheckLanesTestTally fixed = { .random = BUSTER_CHECK_LANES_TEST_SQUEEZE_SEED };
    const uint64_t length = text != NULL ? check_lanes_test_generate(&fixed, text, capacity, BUSTER_CHECK_LANES_TEST_SQUEEZE_FUNCTIONS) : 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/squeeze.c", directory);
    FILE* file = text != NULL ? fopen(path, "wb") : NULL;
    const bool written = file != NULL && fwrite(text, 1, length, file) == length;
    const bool closed = file != NULL && fclose(file) == 0;
    check_lanes_test_expect(tally, written && closed, "write the squeezes' program", path);
    free(text);
    for (uint32_t s = 0; written && closed && s <= BUSTER_ARRAY_LENGTH(check_lanes_test_failures); s += 1)
    {
        if (s != 0)
        {
            snprintf(path, sizeof(path), "%s/failure_%u.c", directory, s - 1);
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            CheckLanesTestUnit unit;
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
            check_lanes_test_expect(tally, unit.ready, "compile a squeezed program to its selected program", path);
            for (uint32_t shape = 0; unit.ready && shape < 2; shape += 1)
            {
                check_lanes_test_squeeze(tally, s == 0 ? &generated : &failing, &unit, &parser, &pp, &identifiers, scalar != 0, shape == 1, s == 0,
                                         s == 0 ? "the squeezes' program" : check_lanes_test_failures[s - 1].name);
            }
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
    }
    char detail[256];
    snprintf(detail, sizeof(detail), "run again for the lowering %llu times, Tier 0 %llu, the code after it %llu",
             (unsigned long long)generated.again[CHECK_PROGRAM_AGAIN_LOWERING], (unsigned long long)generated.again[CHECK_PROGRAM_AGAIN_TIER0],
             (unsigned long long)generated.again[CHECK_PROGRAM_AGAIN_CODE]);
    check_lanes_test_expect(tally,
                            generated.again[CHECK_PROGRAM_AGAIN_LOWERING] != 0 && generated.again[CHECK_PROGRAM_AGAIN_TIER0] != 0 &&
                                generated.again[CHECK_PROGRAM_AGAIN_CODE] != 0,
                            "the squeezes reach each reason a lane short of room has", detail);
    const uint64_t failing_again = failing.again[CHECK_PROGRAM_AGAIN_LOWERING] + failing.again[CHECK_PROGRAM_AGAIN_TIER0] + failing.again[CHECK_PROGRAM_AGAIN_CODE];
    check_lanes_test_expect(tally, failing_again != 0, "a failing program whose lane runs out of room runs again", 0);
    printf("  squeezes: %llu emissions with a lane short of room, the generated program's %s; the failing programs' %llu, %llu run again; the parts that didn't fit taken by lane 0 %llu times, by the others %llu\n",
           (unsigned long long)(generated.emissions + failing.emissions), detail, (unsigned long long)failing.emissions, (unsigned long long)failing_again,
           (unsigned long long)(generated.first_lane + failing.first_lane), (unsigned long long)(generated.other_lanes + failing.other_lanes));
}

// Lanes that don't start (see the file's comment): in a child limited to the processes it has (RLIMIT_NPROC, which
// doesn't limit root), so that lane_run starts no thread, the squeezes' program and each failing program on 8 lanes, by
// each path, with -g's rows and unwind tables and without: one lane's object or failure, run again on one lane. The
// child's alarm ends it if lanes wait for workers that never started, and the parent says how it ended.
static void check_lanes_test_unstarted(CheckLanesTestTally* tally, const char* directory)
{
    if (geteuid() == 0)
    {
        printf("  unstarted: lanes that don't start aren't checked as root, whom RLIMIT_NPROC doesn't limit\n");
    }
    else
    {
        fflush(stdout);
        fflush(stderr);
        const pid_t child = fork();
        if (child == 0)
        {
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
#if !BUSTER_SANITIZER_RESOURCES
            const struct rlimit limit = {
                .rlim_cur = 0,
                .rlim_max = 0,
            };
#endif
            alarm(BUSTER_OBJECT_TEST_RUN_SECONDS);
            CheckLanesTestTally own = { .random = 1 };
#if BUSTER_SANITIZER_RESOURCES
            lane_test_resources.no_threads = true;
            check_lanes_test_expect(&own, lane_test_resources.no_threads, "inject the child's lane-start failure", 0);
#else
            check_lanes_test_expect(&own, setrlimit(RLIMIT_NPROC, &limit) == 0, "limit the child to the processes it has", 0);
#endif
            uint32_t compared = 0;
            char path[PATH_MAX];
            char detail[256];
            for (uint32_t s = 0; s <= BUSTER_ARRAY_LENGTH(check_lanes_test_failures); s += 1)
            {
                if (s == 0)
                {
                    snprintf(path, sizeof(path), "%s/squeeze.c", directory);
                }
                else
                {
                    snprintf(path, sizeof(path), "%s/failure_%u.c", directory, s - 1);
                }
                for (uint32_t scalar = 0; scalar < 2; scalar += 1)
                {
                    CheckLanesTestUnit unit;
                    check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
                    check_lanes_test_expect(&own, unit.ready, "compile a program in the limited child", path);
                    for (uint32_t shape = 0; unit.ready && shape < 2; shape += 1)
                    {
                        CheckLanesTestEmission one = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, scalar != 0, shape == 1, shape == 1);
                        CheckProgramLanes lanes = {
                            .lanes = 8,
                            .every_lane = true,
                        };
                        CheckLanesTestEmission many = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &lanes, scalar != 0, shape == 1, shape == 1);
                        snprintf(detail, sizeof(detail), "%s, %s path%s: status %u/%u, %llu/%llu bytes, %u lanes, again %u", path, scalar != 0 ? "scalar" : "vector",
                                 shape == 1 ? ", -g and unwind tables" : "", (uint32_t)one.status, (uint32_t)many.status, (unsigned long long)one.size,
                                 (unsigned long long)many.size, many.active, (uint32_t)many.again);
                        check_lanes_test_expect(&own, s == 0 ? one.emitted && one.built : !one.emitted,
                                                s == 0 ? "the squeezes' program makes its object on one lane" : "a failing program fails on one lane", detail);
                        check_lanes_test_expect(&own, check_lanes_test_same(&one, &many), "an emission whose lanes don't start makes one lane's", detail);
                        check_lanes_test_expect(&own, many.active == 1 || many.again == CHECK_PROGRAM_AGAIN_START, "lanes that don't start run the bodies again on one lane",
                                                detail);
                        free(one.object);
                        free(many.object);
                        compared += 1;
                    }
                    check_lanes_test_close(&unit);
                    parse_release(&parser);
                    pp_release(&pp);
                    intern_release(&identifiers);
                }
            }
            printf("  unstarted: %u emissions whose lanes didn't start, each one lane's\n", compared);
            fflush(stdout);
            fflush(stderr);
            _exit(own.failures == 0 && compared != 0 ? 0 : 1);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        char ended[96];
        if (!waited)
        {
            snprintf(ended, sizeof(ended), "the child couldn't be started or waited for");
        }
        else if (WIFEXITED(status))
        {
            snprintf(ended, sizeof(ended), "the child exited with status %d", WEXITSTATUS(status));
        }
        else
        {
            snprintf(ended, sizeof(ended), "the child was killed by signal %d%s", WTERMSIG(status), WTERMSIG(status) == SIGALRM ? ", its alarm: it hung" : "");
        }
        check_lanes_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "emissions whose lanes don't start make one lane's, in a limited child",
                                ended);
    }
}

// The address space (see the file's comment): in a child, the squeezes' program by each path, with -g's rows and unwind
// tables and without. An emission on lanes of one must reserve exactly the room lane 0's writer's columns take. Then
// emissions limited (RLIMIT_AS) to less address space than that reservation maps, and to the reservation's and 1 GB
// more, far less than a lane's 64 GB arenas: check_program_emit's, on lanes of one, and on 8 lanes with lane 0 slow, so
// that the others take parts: each one lane's object, lane 0's columns in the caller's scratch at the first limit and in
// their reservation at the second, and the 8 lanes run again on one, their threads not started or their arenas not
// reserved. And a program without bodies: no room reserved for lane 0's columns, and check_program_emit's object with
// no address space at all past its arenas. The child's alarm ends it if an emission hangs, and the parent says how it
// ended.
static void check_lanes_test_address_space(CheckLanesTestTally* tally, const char* directory)
{
    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        alarm(BUSTER_OBJECT_TEST_RUN_SECONDS);
        CheckLanesTestTally own = { .random = 1 };
        uint32_t compared = 0;
        uint64_t largest = 0;
        uint64_t again[CHECK_PROGRAM_AGAIN_CODE + 1] = { 0 };
        char path[PATH_MAX];
        char detail[384];
        snprintf(path, sizeof(path), "%s/squeeze.c", directory);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            CheckLanesTestUnit unit;
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
            check_lanes_test_expect(&own, unit.ready, "compile the squeezes' program in the child", path);
            for (uint32_t shape = 0; unit.ready && shape < 2; shape += 1)
            {
                const bool rows = shape == 1;
                CheckLanesTestEmission one = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, scalar != 0, rows, rows);
                CheckProgramLanes probe = { .lanes = 1 };
                CheckLanesTestEmission probed = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &probe, scalar != 0, rows, rows);
                const uint64_t room = probed.columns_capacity;
                largest = room > largest ? room : largest;
                snprintf(detail, sizeof(detail), "%s path%s: %llu bytes reserved for lane 0's columns, %llu taken", scalar != 0 ? "scalar" : "vector",
                         rows ? ", -g and unwind tables" : "", (unsigned long long)room, (unsigned long long)probed.columns_length);
                check_lanes_test_expect(&own, one.emitted && one.built, "the squeezes' program makes its object on one lane", detail);
                check_lanes_test_expect(&own, check_lanes_test_same(&one, &probed) && room != 0 && probed.columns_length == room,
                                        "lane 0's writer's columns take exactly the room reserved for them", detail);
                for (uint32_t bound = 0; room != 0 && bound < 2; bound += 1)
                {
                    const uint64_t allowed = bound == 0 ? room / 2 : arena_mapping_bytes(room) + (UINT64_C(1) << 30);
                    for (uint32_t kind = 0; kind < 3; kind += 1)
                    {
                        CheckProgramLanes lanes = {
                            .lanes = kind == 2 ? 8 : 1,
                            .every_lane = true,
                            .slow_lane = kind == 2 ? 1 : 0,
                            .slow_microseconds = 500,
                        };
                        CheckLanesTestEmission limited =
                            check_lanes_test_emit_limited(&unit, &parser, &pp, &identifiers, kind == 0 ? NULL : &lanes, scalar != 0, rows, rows, allowed, UINT64_MAX);
                        snprintf(detail, sizeof(detail), "%s path%s, %s, %llu bytes more: status %u/%u, %llu/%llu bytes, columns' arena %llu bytes, active %u, again %u, reason %s",
                                 scalar != 0 ? "scalar" : "vector", rows ? ", -g and unwind tables" : "",
                                 kind == 0 ? "check_program_emit" : kind == 1 ? "lanes of one" : "8 lanes", (unsigned long long)allowed, (uint32_t)one.status,
                                 (uint32_t)limited.status, (unsigned long long)one.size, (unsigned long long)limited.size,
                                 (unsigned long long)limited.columns_capacity, (uint32_t)limited.active, (uint32_t)limited.again,
                                 limited.reason != NULL ? limited.reason : "-");
                        check_lanes_test_expect(&own, limited.limited, "limit the child's address space for an emission", detail);
                        check_lanes_test_expect(&own, check_lanes_test_same(&one, &limited), "an emission in a limited address space makes one lane's", detail);
                        check_lanes_test_expect(&own, kind == 0 || limited.columns_capacity == (bound == 0 ? 0 : room),
                                                bound == 0 ? "lane 0's columns go in the caller's scratch without room for their own"
                                                           : "lane 0's columns go in the room reserved for them",
                                                detail);
                        // The room that keeps only lane 0's columns out turns every lane arena's reservation away,
                        // so a run that got lanes must go again; under the larger room a throttled or pool-reusing
                        // run can legitimately take all of its lanes without a rerun.
                        check_lanes_test_expect(&own, kind != 2 || bound != 0 || limited.active < 2 || limited.again != CHECK_PROGRAM_AGAIN_NONE,
                                                "8 lanes without their arenas run again on one", detail);
                        again[limited.again] += kind == 2 ? 1 : 0;
                        free(limited.object);
                        compared += 1;
                    }
                }
                free(one.object);
                free(probed.object);
            }
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
        // A program without bodies, whose writer never begins: no room reserved for its columns, and its emission needs no
        // address space at all past its arenas.
        snprintf(path, sizeof(path), "%s/no_bodies.c", directory);
        FILE* file = fopen(path, "wb");
        const bool written = file != NULL && fputs("int counter = 3;\nstatic const char name[] = \"lanes\";\nconst char* first = name;\n", file) >= 0;
        const bool closed = file != NULL && fclose(file) == 0;
        check_lanes_test_expect(&own, written && closed, "write a program without bodies", path);
        CheckLanesTestUnit unit;
        check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, false);
        check_lanes_test_expect(&own, written && closed && unit.ready && unit.selected.program.body_count == 0, "compile a program without bodies", path);
        if (unit.ready)
        {
            CheckLanesTestEmission one = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, false, false, false);
            CheckProgramLanes probe = { .lanes = 1 };
            CheckLanesTestEmission probed = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &probe, false, false, false);
            CheckLanesTestEmission limited = check_lanes_test_emit_limited(&unit, &parser, &pp, &identifiers, NULL, false, false, false, 0, UINT64_MAX);
            snprintf(detail, sizeof(detail), "%llu/%llu/%llu bytes, columns' arena %llu bytes", (unsigned long long)one.size, (unsigned long long)probed.size,
                     (unsigned long long)limited.size, (unsigned long long)probed.columns_capacity);
            check_lanes_test_expect(&own, one.emitted && one.built && check_lanes_test_same(&one, &probed) && probed.columns_capacity == 0,
                                    "an emission without bodies reserves no room for lane 0's columns", detail);
            check_lanes_test_expect(&own, limited.limited && check_lanes_test_same(&one, &limited),
                                    "an emission without bodies needs no address space past its arenas", detail);
            free(one.object);
            free(probed.object);
            free(limited.object);
            compared += 1;
        }
        check_lanes_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        printf("  address space: %u emissions in a limited address space, each one lane's, lane 0's columns reserving at most %llu bytes; 8 lanes "
               "run again %llu times as their threads didn't start, %llu as their arenas weren't reserved\n",
               compared, (unsigned long long)largest, (unsigned long long)again[CHECK_PROGRAM_AGAIN_START],
               (unsigned long long)again[CHECK_PROGRAM_AGAIN_LOWERING]);
        fflush(stdout);
        fflush(stderr);
        _exit(own.failures == 0 && compared != 0 ? 0 : 1);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    char ended[96];
    if (!waited)
    {
        snprintf(ended, sizeof(ended), "the child couldn't be started or waited for");
    }
    else if (WIFEXITED(status))
    {
        snprintf(ended, sizeof(ended), "the child exited with status %d", WEXITSTATUS(status));
    }
    else
    {
        snprintf(ended, sizeof(ended), "the child was killed by signal %d%s", WTERMSIG(status), WTERMSIG(status) == SIGALRM ? ", its alarm: it hung" : "");
    }
    check_lanes_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "emissions in a limited address space make one lane's, in a child",
                            ended);
}

// The caller's scratch (see the file's comment): the squeezes' program by each path, with -g's rows and unwind tables and
// without, emitted by check_program_emit and on lanes of one with the least room of the caller's scratch that they take,
// found by halving the room between none and 16 GB to a page: each one lane's object, and that room less than lane 0's
// columns take, which then don't fit in the scratch, so that their own arena's room mustn't depend on it.
static void check_lanes_test_scratch(CheckLanesTestTally* tally, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    char path[PATH_MAX];
    char detail[256];
    uint32_t searched = 0;
    uint64_t largest = 0;
    uint64_t columns = 0;
    snprintf(path, sizeof(path), "%s/squeeze.c", directory);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        CheckLanesTestUnit unit;
        check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
        check_lanes_test_expect(tally, unit.ready, "compile the squeezes' program", path);
        for (uint32_t shape = 0; unit.ready && shape < 2; shape += 1)
        {
            const bool rows = shape == 1;
            CheckLanesTestEmission one = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, NULL, scalar != 0, rows, rows);
            CheckProgramLanes probe = { .lanes = 1 };
            CheckLanesTestEmission probed = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &probe, scalar != 0, rows, rows);
            columns = probed.columns_capacity > columns ? probed.columns_capacity : columns;
            for (uint32_t kind = 0; kind < 2; kind += 1)
            {
                uint64_t fails = 0;
                uint64_t fits = UINT64_C(1) << 34;
                while (fits - fails > BUSTER_ARENA_PAGE_BYTES)
                {
                    const uint64_t middle = fails + (fits - fails) / 2;
                    CheckProgramLanes lanes = { .lanes = 1 };
                    CheckLanesTestEmission squeezed =
                        check_lanes_test_emit_limited(&unit, &parser, &pp, &identifiers, kind == 0 ? NULL : &lanes, scalar != 0, rows, rows, UINT64_MAX, middle);
                    const bool same = check_lanes_test_same(&one, &squeezed);
                    fits = same ? middle : fits;
                    fails = same ? fails : middle;
                    free(squeezed.object);
                }
                CheckProgramLanes lanes = { .lanes = 1 };
                CheckLanesTestEmission least =
                    check_lanes_test_emit_limited(&unit, &parser, &pp, &identifiers, kind == 0 ? NULL : &lanes, scalar != 0, rows, rows, UINT64_MAX, fits);
                snprintf(detail, sizeof(detail), "%s path%s, %s: %llu bytes of scratch, %llu of lane 0's columns", scalar != 0 ? "scalar" : "vector",
                         rows ? ", -g and unwind tables" : "", kind == 0 ? "check_program_emit" : "lanes of one", (unsigned long long)fits,
                         (unsigned long long)probed.columns_capacity);
                check_lanes_test_expect(tally, check_lanes_test_same(&one, &least) && (kind == 0 || least.columns_capacity == probed.columns_capacity),
                                        "an emission with the least scratch it takes makes one lane's object, its columns in their own arena", detail);
                check_lanes_test_expect(tally, probed.columns_capacity != 0 && fits < probed.columns_capacity,
                                        "the least scratch an emission takes has less room than lane 0's columns", detail);
                largest = fits > largest ? fits : largest;
                searched += 1;
                free(least.object);
            }
            free(one.object);
            free(probed.object);
        }
        check_lanes_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    printf("  scratch: %u emissions with the least scratch they take, at most %llu bytes, against lane 0's columns' %llu, each one lane's\n", searched,
           (unsigned long long)largest, (unsigned long long)columns);
}

// Lane 0's columns kept from one emission to the next (#442): the squeezes' program emitted three times on lanes of one,
// by each path, the lanes settled after each as check_driver_object settles a units' lane's (check_program_lanes_settle):
// each emission makes the first one's code, in the first lanes' scratch the first one reserved, which each settle keeps
// with no lane's arenas in use; then check_program_lanes_release releases it.
static void check_lanes_test_settle(CheckLanesTestTally* tally, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    char path[PATH_MAX];
    char detail[256];
    snprintf(path, sizeof(path), "%s/squeeze.c", directory);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        CheckLanesTestUnit unit;
        check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar != 0);
        check_lanes_test_expect(tally, unit.ready, "compile the squeezes' program for lane 0's kept columns", path);
        Arena output = arena_reserve(UINT64_C(1) << 32);
        Arena bytes = arena_reserve(UINT64_C(1) << 32);
        Arena scratch = arena_reserve(UINT64_C(1) << 34);
        CheckProgramLanes lanes = { .lanes = 1 };
        const bool ready = unit.ready && output.base != 0 && bytes.base != 0 && scratch.base != 0;
        const uint8_t* first_columns = 0;
        uint64_t first_code = 0;
        uint32_t kept = 0;
        for (uint32_t emission = 0; ready && emission < 3; emission += 1)
        {
            CheckProgramCode code = { 0 };
            const bool emitted = check_program_emit_lanes(&code, &unit.selected.program, &parser, &identifiers, unit.selected.data.capacity, scalar != 0, &output,
                                                          &bytes, &scratch, NULL, NULL, false, true, &lanes);
            const uint8_t* columns = lanes.arenas[0].scratch.base;
            first_columns = emission == 0 ? columns : first_columns;
            first_code = emission == 0 ? code.code_bytes : first_code;
            check_program_lanes_settle(&lanes);
            kept += emitted && columns != 0 && columns == first_columns && code.code_bytes == first_code && lanes.arenas_used == 0 &&
                    lanes.arenas[0].scratch.base == columns;
            arena_pop_to(&output, 0);
            arena_pop_to(&bytes, 0);
            arena_pop_to(&scratch, 0);
        }
        snprintf(detail, sizeof(detail), "%s path: %u of 3 emissions kept the first one's columns and made its %llu bytes of code", scalar != 0 ? "scalar" : "vector",
                 kept, (unsigned long long)first_code);
        check_lanes_test_expect(tally, kept == 3, "lanes settled after each emission keep lane 0's columns for the next, which makes the same code", detail);
        check_program_lanes_release(&lanes);
        check_lanes_test_expect(tally, lanes.arenas[0].scratch.base == 0, "releasing the settled lanes releases lane 0's columns", detail);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        check_lanes_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

// The lane rule (see the file's comment): a generated program's lanes by default, with room for 8.
static void check_lanes_test_rule(CheckLanesTestTally* tally, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    static const uint32_t sizes[] = { 1, 4, 40, 400, 1200 };
    const uint64_t capacity = UINT64_C(1) << 24;
    char* text = malloc(capacity);
    char detail[256];
    for (uint32_t s = 0; text != NULL && s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
    {
        const uint64_t length = check_lanes_test_generate(tally, text, capacity, sizes[s]);
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/rule_%u.c", directory, s);
        FILE* file = fopen(path, "wb");
        const bool written = file != NULL && fwrite(text, 1, length, file) == length;
        const bool closed = file != NULL && fclose(file) == 0;
        CheckLanesTestUnit unit;
        check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, false);
        check_lanes_test_expect(tally, written && closed && unit.ready, "compile the rule's program", path);
        if (unit.ready)
        {
            const CheckProgram* program = &unit.selected.program;
            uint64_t total = 0;
            uint64_t largest = 0;
            for (uint32_t b = 0; b < program->body_count; b += 1)
            {
                const uint64_t tokens = (uint64_t)parser.tree.parents[program->bodies[b].opening] - program->bodies[b].opening + 1;
                total += tokens;
                largest = tokens > largest ? tokens : largest;
            }
            const uint64_t paid = 1 + (total > 2 * largest ? total - 2 * largest : 0) / BUSTER_CHECK_EMIT_LANE_TOKENS;
            const uint32_t bodies = program->body_count != 0 ? program->body_count : 1;
            const uint64_t expected = paid < 8 ? (paid < bodies ? paid : bodies) : (8 < bodies ? 8 : bodies);
            CheckProgramLanes lanes = { .lanes = 8 };
            CheckLanesTestEmission emission = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &lanes, false, false, false);
            snprintf(detail, sizeof(detail), "%u functions, %llu tokens in bodies, the largest %llu: %u lanes ran, %llu expected", sizes[s],
                     (unsigned long long)total, (unsigned long long)largest, emission.active, (unsigned long long)expected);
            check_lanes_test_expect(tally, emission.emitted && emission.active == expected, "the default lanes are the rule's", detail);
            printf("  rule: %s\n", detail);
            free(emission.object);
        }
        check_lanes_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    free(text);
}

// Runs `arguments` (arguments[0] the executable), its errors into `errors`: its exit status, or -1.
static int check_lanes_test_run(char* const* arguments, const char* errors)
{
    return object_test_run(arguments, "/dev/null", errors);
}

// Runs `arguments` as check_lanes_test_run does, in a child limited to the processes it has (RLIMIT_NPROC, which doesn't
// limit root), so that it can start no thread: its exit status, or -1. An alarm, which execv keeps, ends a child that
// hangs.
static int check_lanes_test_run_unstarted(char* const* arguments, const char* errors)
{
    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0)
    {
#if BUSTER_SANITIZER_RESOURCES
        const bool limited = setenv("BUSTER_TEST_NO_LANE_THREADS", "1", 1) == 0;
#else
        const struct rlimit limit = {
            .rlim_cur = 0,
            .rlim_max = 0,
        };
        const bool limited = setrlimit(RLIMIT_NPROC, &limit) == 0;
#endif
        const int out = open("/dev/null", O_WRONLY);
        const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0 && err >= 0 && dup2(out, 1) == 1 && dup2(err, 2) == 2 && limited)
        {
            alarm(BUSTER_OBJECT_TEST_RUN_SECONDS);
            execv(arguments[0], arguments);
        }
        _exit(127);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The driver (see the file's comment) on four of the generated programs `program_N.c` in `directory`, and with their
// identifiers' ids in another order, from `id_order` on.
static void check_lanes_test_driver(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs, uint64_t id_order)
{
    static const char* const lane_counts[] = { "1", "3", "8", NULL };
    const uint32_t chosen[] = { 2, 3, 5, programs };
    char detail[PATH_MAX + 128];
    uint32_t compared = 0;
    uint32_t ordered = 0;
    uint32_t driven = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(chosen) && chosen[i] < programs + 2; i += 1)
    {
        const uint32_t p = chosen[i];
        driven = i + 1;
        char source[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(source, sizeof(source), "%s/program_%u.c", directory, p);
        snprintf(errors, sizeof(errors), "%s/driver_%u.errors", directory, p);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            int statuses[BUSTER_ARRAY_LENGTH(lane_counts)] = { 0 };
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
            {
                char object[PATH_MAX];
                char program[PATH_MAX];
                snprintf(object, sizeof(object), "%s/driver_%u_%u_%u.o", directory, p, scalar, c);
                snprintf(program, sizeof(program), "%s/driver_%u_%u_%u", directory, p, scalar, c);
                char* object_arguments[12] = { (char*)executable };
                char* program_arguments[12] = { (char*)executable };
                uint32_t count = 1;
                if (scalar != 0)
                {
                    object_arguments[count] = (char*)"--scalar";
                    program_arguments[count++] = (char*)"--scalar";
                }
                if (lane_counts[c] != NULL)
                {
                    object_arguments[count] = (char*)"--lanes";
                    program_arguments[count++] = (char*)"--lanes";
                    object_arguments[count] = (char*)lane_counts[c];
                    program_arguments[count++] = (char*)lane_counts[c];
                }
                object_arguments[count] = (char*)"-c";
                program_arguments[count] = (char*)"-o";
                object_arguments[count + 1] = (char*)"-o";
                program_arguments[count + 1] = program;
                object_arguments[count + 2] = object;
                program_arguments[count + 2] = source;
                object_arguments[count + 3] = source;
                const bool objected = check_lanes_test_run(object_arguments, errors) == 0;
                const bool linked = check_lanes_test_run(program_arguments, errors) == 0;
                char* run_arguments[] = { program, NULL };
                statuses[c] = linked ? check_lanes_test_run(run_arguments, "/dev/null") : -1;
                snprintf(detail, sizeof(detail), "%s, %s path, --lanes %s", source, scalar != 0 ? "scalar" : "vector", lane_counts[c] != NULL ? lane_counts[c] : "by default");
                check_lanes_test_expect(tally, objected && linked && statuses[c] >= 0, "the driver writes the object and the executable, which runs", detail);
                if (c != 0)
                {
                    char first_object[PATH_MAX];
                    char first_program[PATH_MAX];
                    snprintf(first_object, sizeof(first_object), "%s/driver_%u_%u_0.o", directory, p, scalar);
                    snprintf(first_program, sizeof(first_program), "%s/driver_%u_%u_0", directory, p, scalar);
                    check_lanes_test_expect(tally, check_test_driver_same_files(object, first_object), "the driver's object on lanes is one lane's", detail);
                    check_lanes_test_expect(tally, check_test_driver_same_files(program, first_program), "the driver's executable on lanes is one lane's", detail);
                    check_lanes_test_expect(tally, statuses[c] == statuses[0], "the executable on lanes exits as one lane's does", detail);
                    compared += 1;
                }
            }
            // With the identifiers' ids in another order (#153's --id-order), by --lanes 8: one lane's object and executable.
            char order[48];
            char object[PATH_MAX];
            char program[PATH_MAX];
            char first_object[PATH_MAX];
            char first_program[PATH_MAX];
            snprintf(order, sizeof(order), "--id-order=%llu", (unsigned long long)(id_order + p));
            snprintf(object, sizeof(object), "%s/driver_%u_%u_ordered.o", directory, p, scalar);
            snprintf(program, sizeof(program), "%s/driver_%u_%u_ordered", directory, p, scalar);
            snprintf(first_object, sizeof(first_object), "%s/driver_%u_%u_0.o", directory, p, scalar);
            snprintf(first_program, sizeof(first_program), "%s/driver_%u_%u_0", directory, p, scalar);
            char* object_arguments[10] = { (char*)executable };
            char* program_arguments[10] = { (char*)executable };
            uint32_t count = 1;
            if (scalar != 0)
            {
                object_arguments[count] = (char*)"--scalar";
                program_arguments[count++] = (char*)"--scalar";
            }
            object_arguments[count] = (char*)"--lanes=8";
            program_arguments[count++] = (char*)"--lanes=8";
            object_arguments[count] = order;
            program_arguments[count++] = order;
            object_arguments[count] = (char*)"-c";
            program_arguments[count] = (char*)"-o";
            object_arguments[count + 1] = (char*)"-o";
            program_arguments[count + 1] = program;
            object_arguments[count + 2] = object;
            program_arguments[count + 2] = source;
            object_arguments[count + 3] = source;
            const bool objected = check_lanes_test_run(object_arguments, errors) == 0;
            const bool linked = check_lanes_test_run(program_arguments, errors) == 0;
            char* run_arguments[] = { program, NULL };
            const int status = linked ? check_lanes_test_run(run_arguments, "/dev/null") : -1;
            snprintf(detail, sizeof(detail), "%s, %s path, --lanes 8 %s", source, scalar != 0 ? "scalar" : "vector", order);
            check_lanes_test_expect(tally, objected && check_test_driver_same_files(object, first_object), "the driver's object with the ids in another order is one lane's",
                                    detail);
            check_lanes_test_expect(tally, linked && check_test_driver_same_files(program, first_program) && status == statuses[0],
                                    "the driver's executable with the ids in another order is one lane's, and exits as it does", detail);
            ordered += 1;
        }
    }
    printf("  driver: %u objects and executables on lanes and %u with the ids in another order, each one lane's\n", compared, ordered);
    // With lanes that don't start: two of the programs' objects and executables by --lanes 8, by each path, each the one
    // lane's from above. The driver starts threads for the functions' lanes alone; it writes the executable on one lane.
    uint32_t unstarted = 0;
    if (geteuid() == 0)
    {
        printf("  driver: lanes that don't start aren't checked as root, whom RLIMIT_NPROC doesn't limit\n");
    }
    for (uint32_t i = 0; geteuid() != 0 && i < driven; i += 3)
    {
        const uint32_t p = chosen[i];
        char source[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(source, sizeof(source), "%s/program_%u.c", directory, p);
        snprintf(errors, sizeof(errors), "%s/driver_%u_unstarted.errors", directory, p);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char object[PATH_MAX];
            char program[PATH_MAX];
            char first_object[PATH_MAX];
            char first_program[PATH_MAX];
            snprintf(object, sizeof(object), "%s/driver_%u_%u_unstarted.o", directory, p, scalar);
            snprintf(program, sizeof(program), "%s/driver_%u_%u_unstarted", directory, p, scalar);
            snprintf(first_object, sizeof(first_object), "%s/driver_%u_%u_0.o", directory, p, scalar);
            snprintf(first_program, sizeof(first_program), "%s/driver_%u_%u_0", directory, p, scalar);
            char* object_arguments[8] = { (char*)executable };
            char* program_arguments[8] = { (char*)executable };
            uint32_t count = 1;
            if (scalar != 0)
            {
                object_arguments[count] = (char*)"--scalar";
                program_arguments[count++] = (char*)"--scalar";
            }
            object_arguments[count] = (char*)"--lanes=8";
            program_arguments[count++] = (char*)"--lanes=8";
            object_arguments[count] = (char*)"-c";
            program_arguments[count] = (char*)"-o";
            object_arguments[count + 1] = (char*)"-o";
            program_arguments[count + 1] = program;
            object_arguments[count + 2] = object;
            program_arguments[count + 2] = source;
            object_arguments[count + 3] = source;
            snprintf(detail, sizeof(detail), "%s, %s path, --lanes 8 that don't start", source, scalar != 0 ? "scalar" : "vector");
            check_lanes_test_expect(tally,
                                    check_lanes_test_run_unstarted(object_arguments, errors) == 0 && check_test_driver_same_files(object, first_object),
                                    "the driver's object whose lanes don't start is one lane's", detail);
            check_lanes_test_expect(tally,
                                    check_lanes_test_run_unstarted(program_arguments, errors) == 0 && check_test_driver_same_files(program, first_program),
                                    "the driver's executable whose lanes don't start is one lane's", detail);
            unstarted += 1;
        }
    }
    printf("  driver: %u objects and executables whose lanes didn't start, each one lane's\n", unstarted);
    // The option's values: 1 to 64 lanes, as `--lanes N` or `--lanes=N`; any other is the usage, with status 1.
    static const char* const refused[] = { "0", "x", "65", "8x", "" };
    char source[PATH_MAX];
    char object[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(source, sizeof(source), "%s/program_0.c", directory);
    snprintf(object, sizeof(object), "%s/driver_option.o", directory);
    snprintf(errors, sizeof(errors), "%s/driver_option.errors", directory);
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(refused); r += 1)
    {
        char* arguments[] = { (char*)executable, (char*)"--lanes", (char*)refused[r], (char*)"-c", (char*)"-o", object, source, NULL };
        snprintf(detail, sizeof(detail), "--lanes \"%s\"", refused[r]);
        check_lanes_test_expect(tally, check_lanes_test_run(arguments, errors) == 1, "the driver refuses a lane count it can't take", detail);
        char* budget_arguments[] = { (char*)executable, (char*)"--worker-budget", (char*)refused[r], (char*)"-c", (char*)"-o", object, source, NULL };
        check_lanes_test_expect(tally, check_lanes_test_run(budget_arguments, errors) == 1, "the driver refuses an invalid total worker budget", detail);
    }
    char* missing[] = { (char*)executable, (char*)"-c", (char*)"-o", object, source, (char*)"--lanes", NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(missing, errors) == 1, "the driver refuses --lanes without its value", 0);
    // --id-order's value: a number, as `--id-order N` or `--id-order=N`; any other is the usage, with status 1.
    static const char* const unordered[] = { "x", "8x", "", "-" };
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(unordered); r += 1)
    {
        char* arguments[] = { (char*)executable, (char*)"--id-order", (char*)unordered[r], (char*)"-c", (char*)"-o", object, source, NULL };
        snprintf(detail, sizeof(detail), "--id-order \"%s\"", unordered[r]);
        check_lanes_test_expect(tally, check_lanes_test_run(arguments, errors) == 1, "the driver refuses an id order it can't take", detail);
    }
    char* order_missing[] = { (char*)executable, (char*)"-c", (char*)"-o", object, source, (char*)"--id-order", NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(order_missing, errors) == 1, "the driver refuses --id-order without its value", NULL);
    char* order_separate[] = { (char*)executable, (char*)"--id-order", (char*)"7", (char*)"-c", (char*)"-o", object, source, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(order_separate, errors) == 0, "the driver takes --id-order 7", NULL);
    char* joined[] = { (char*)executable, (char*)"--lanes=8", (char*)"-c", (char*)"-o", object, source, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(joined, errors) == 0, "the driver takes --lanes=8", 0);
    char* capped[] = { (char*)executable, (char*)"--lanes=8", (char*)"--worker-budget=1", (char*)"-c", (char*)"-o", object, source, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(capped, errors) == 0, "the driver takes an explicit calling-thread-only worker budget", NULL);
    char* budget_missing[] = { (char*)executable, (char*)"-c", (char*)"-o", object, source, (char*)"--worker-budget", NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(budget_missing, errors) == 1, "the driver refuses --worker-budget without its value", NULL);
    static const char* const non_dry_flags[] = { "MAKEFLAGS= -I/usr/include", "MAKEFLAGS= -Onone", "MAKEFLAGS= -I/opt/inc -j2" };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(non_dry_flags); f += 1)
    {
        unlink(object);
        char* compile[] = { "env", (char*)non_dry_flags[f], (char*)executable, "-c", "-o", object, source, NULL };
        struct stat output_stat;
        const int status = check_lanes_test_run(compile, errors);
        check_lanes_test_expect(tally, status == 0 && stat(object, &output_stat) == 0 && output_stat.st_size > 0,
                                "make option arguments containing n still compile an object", non_dry_flags[f]);
    }
    static const char* const dry_routes[] = { "-c", "-E", "parse" };
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(dry_routes); r += 1)
    {
        unlink(object);
        char* dry[] = { "env", "MAKEFLAGS=n", (char*)executable, (char*)dry_routes[r], "-o", object,
                        "/buster-nonexistent-dry-run-source.c", NULL };
        const int status = check_lanes_test_run(dry, errors);
        struct stat output_stat;
        struct stat error_stat;
        check_lanes_test_expect(tally, status == 0 && stat(object, &output_stat) != 0 && stat(errors, &error_stat) == 0 && error_stat.st_size == 0,
                                "make dry runs do no source work, output or diagnostics in C, -E and parse routes", dry_routes[r]);
    }
}

// The jobserver's line of a --time-stages report at `path` (check_driver_command's): its limit, request, grant, team at
// its end, growth and the lanes that took a part, in `reported`; false if it has none, or another mode's.
static bool check_lanes_test_report_budget(const char* path, uint32_t* reported)
{
    char mode[32] = { 0 };
    bool found = false;
    FILE* file = fopen(path, "r");
    char line[256];
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        found |= sscanf(line, "worker-budget\tmode=%31[^\t]\tlimit=%u\trequested=%u\tgranted=%u\tactive=%u\tgrown=%u\tworked=%u", mode, reported,
                        reported + 1, reported + 2, reported + 3, reported + 4, reported + 5) == 7 &&
                 strcmp(mode, "jobserver") == 0;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return found;
}

// The growth gate (check_program_emit_grow) alone, under a jobserver pipe of 7 tokens, as the default policy takes it
// (see the file's comment).
static void check_lanes_test_grow_gate(CheckLanesTestTally* tally)
{
    // A team of `admitted` of up to `lanes` lanes, `ticket` of 100 parts' 200 items taken (their lowering, then their
    // code), the parts from its place on of `rest` tokens, --lanes N's or not, and the lanes it must admit. With 1 of 8
    // admitted and 7 spare, the least work left that pays is the ceiling of BUSTER_CHECK_EMIT_GROW_TOKENS × 1 × 8 / 7:
    // as many tokens with no part lowered yet, and with every part lowered, the tokens whose code's share is that.
    typedef struct CheckLanesGate CheckLanesGate;
    struct CheckLanesGate
    {
        uint64_t ticket;
        uint64_t rest;
        uint32_t admitted;
        uint32_t lanes;
        bool every_lane;
        uint32_t more;
        const char* what;
    };
    const uint64_t least = ((uint64_t)BUSTER_CHECK_EMIT_GROW_TOKENS * 8 + 6) / 7;
    const uint64_t code = 16 - BUSTER_CHECK_EMIT_GROW_LOWER_SIXTEENTHS;
    const uint64_t lowered = (least * 16 + code - 1) / code;
    const CheckLanesGate gates[] = {
        { 0, least - 1, 1, 8, false, 0, "parts left just short of paying for a guardian don't grow the team" },
        { 0, least, 1, 8, false, 7, "parts left that just pay for a guardian grow the team by every spare token" },
        { 0, 1, 1, 8, false, 0, "too little work left doesn't grow the team, whatever is spare" },
        { 0, 1, 1, 8, true, 7, "--lanes N grows the team whatever the parts left pay" },
        { 100, lowered - 1, 1, 8, false, 0, "lowered parts whose code's share falls just short of paying don't grow the team" },
        { 100, lowered, 1, 8, false, 7, "lowered parts whose code's share just pays grow the team" },
        { 198, UINT64_C(1) << 40, 1, 8, false, 2, "a team grows by no more lanes than items left" },
        { 0, UINT64_C(1) << 40, 6, 8, false, 2, "a team grows by no more lanes than it may take" },
        { 0, UINT64_C(1) << 40, 1, 16, false, 7, "a team grows by no more lanes than spare tokens" },
        { 200, UINT64_C(1) << 40, 1, 8, false, 0, "a team with no item left doesn't grow" },
    };
    static const uint8_t tokens[] = { 1, 2, 3, 4, 5, 6, 7 };
    int fds[2] = { -1, -1 };
    const bool ready = worker_budget_test_pipe_open(fds, O_CLOEXEC) == 0 && write(fds[1], tokens, sizeof(tokens)) == (ssize_t)sizeof(tokens);
    check_lanes_test_expect(tally, ready, "the growth gate's jobserver pipe holds its 7 tokens", NULL);
    char flags[96];
    snprintf(flags, sizeof(flags), "-j --jobserver-auth=%d,%d", fds[0], fds[1]);
    WorkerBudget budget;
    worker_budget_initialize(&budget, flags, 64);
    static uint64_t rest[101];
    for (uint32_t g = 0; ready && g < BUSTER_ARRAY_LENGTH(gates); g += 1)
    {
        const CheckLanesGate* gate = gates + g;
        CheckProgramLanes lanes = { .every_lane = gate->every_lane };
        CheckEmitRun run = { .lanes = &lanes };
        CheckEmitGrowth growth = { .ticket = gate->ticket, .parts = 100, .rest = rest };
        // The tokens from the place of the part the ticket's item is of on, and with no part lowered, every part's code.
        rest[gate->ticket < 100 ? gate->ticket : gate->ticket - 100] = gate->rest;
        rest[0] = gate->ticket < 100 ? gate->rest : rest[0];
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t more = check_program_emit_grow(&run, &growth, gate->admitted, gate->lanes, &budget, &lease);
        // A guardian only for a growth, and its tokens only while it holds them.
        const bool guarded = lease.child > 0;
        const int held = worker_budget_test_available(fds[0]);
        const bool returned = worker_budget_release(&lease);
        char detail[96];
        snprintf(detail, sizeof(detail), "admitted %u more, expected %u; %d tokens left in the pipe", more, gate->more, held);
        check_lanes_test_expect(tally, more == gate->more && guarded == (more != 0) && held == (int)(7 - more) && returned &&
                                           worker_budget_test_available(fds[0]) == 7,
                                gate->what, detail);
    }
    // A guardian that took no token, as when another recipe takes the spare tokens between FIONREAD and its read, goes
    // at once rather than hold one of the team's leases. A budget whose reader is a file of 7 bytes makes that state:
    // FIONREAD counts them, and a guardian, which takes a pipe's alone (F_GETPIPE_SZ), takes none.
    WorkerBudget unread;
    worker_budget_initialize(&unread, flags, 64);
    // MFD_CLOEXEC, which <sys/mman.h> declares only with _GNU_SOURCE.
    const long file = ready && unread.mode == WORKER_BUDGET_JOBSERVER ? syscall(SYS_memfd_create, "buster-grow-gate", 1u) : -1;
    const bool filled = file >= 0 && write((int)file, tokens, sizeof(tokens)) == (ssize_t)sizeof(tokens) && lseek((int)file, 0, SEEK_SET) == 0;
    if (file >= 0)
    {
        close(unread.read_fd);
        unread.read_fd = (int)file;
    }
    WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
    const uint32_t taken = filled ? worker_budget_grow(&unread, 7, &lease) : 1;
    const bool guarded = lease.child > 0;
    const bool released = worker_budget_release(&lease);
    check_lanes_test_expect(tally, filled && taken == 0 && guarded && released && unread.mode == WORKER_BUDGET_JOBSERVER,
                            "a guardian that finds none of the tokens FIONREAD counted takes none and holds its lease", NULL);
    CheckProgramLanes lanes = { 0 };
    CheckEmitRun run = { .lanes = &lanes };
    CheckEmitGrowth growth = { .parts = 100, .rest = rest };
    rest[0] = least;
    const uint32_t more = filled ? check_program_emit_grow(&run, &growth, 1, 8, &unread, &lease) : 1;
    char detail[96];
    snprintf(detail, sizeof(detail), "admitted %u more; lease of guardian %d", more, (int)lease.child);
    check_lanes_test_expect(tally, filled && more == 0 && lease.child < 0 && lease.control < 0 && lease.reply < 0 && !unread.return_failed,
                            "a guardian that took no token is released at once, not left holding one of the team's leases", detail);
    worker_budget_release(&lease);
    worker_budget_close(&unread);
    worker_budget_close(&budget);
    check_lanes_test_expect(tally, !ready || worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)), "the growth gate's tokens all come back once", NULL);
    if (fds[0] >= 0)
    {
        close(fds[0]);
    }
    if (fds[1] >= 0)
    {
        close(fds[1]);
    }
}

// A growing team in the driver (#673's tail; see the file's comment): the first generated program of 600 functions,
// `program_N.c` for N `programs`, by --lanes 4, and programs of 4,000 and of 200 functions of the test's own by the
// default policy, under a jobserver pipe of a case's tokens, lane 0 writing a case's more after its first item
// (BUSTER_TEST_WORKER_SUPPLY).
static void check_lanes_test_driver_growth(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs)
{
    // A case's program (0 the 600 functions', 1 the 4,000's, 2 the 200's), the tokens in the pipe as the driver starts,
    // those its lane 0 supplies, and its options: --lanes 4 (every lane, whatever its parts pay), or the default policy,
    // with --worker-budget 3 or without. The 4,000 functions' parts pay for every growth here. The 200's ask for more
    // than one lane, so that their team reaches the gate (100 functions sit at the lane rule's first step), and their
    // parts left pay for none (a team first grew at about 400).
    typedef struct CheckLanesGrowthCase CheckLanesGrowthCase;
    struct CheckLanesGrowthCase
    {
        uint32_t program;
        uint32_t initial;
        uint32_t supply;
        const char* options[2];
    };
    static const CheckLanesGrowthCase cases[] = {
        { 0, 0, 0, { "--lanes", "4" } },
        { 0, 1, 0, { "--lanes", "4" } },
        { 0, 0, 3, { "--lanes", "4" } },
        { 0, 1, 2, { "--lanes", "4" } },
        { 0, 0, 5, { "--lanes", "4" } },
        { 1, 0, 5, { NULL, NULL } },
        { 1, 0, 5, { "--worker-budget", "3" } },
        { 1, 1, 0, { "--worker-budget", "3" } },
        { 2, 0, 5, { NULL, NULL } },
    };
    static const uint32_t sizes[] = { 0, 4000, 200 };
    const uint32_t cpus = check_driver_cpus();
    // The cores of those CPUs, whose lanes the default policy asks for (lane_test_process_cores, as the session's test
    // counts them).
    const uint32_t listed = lane_test_process_cores();
    const uint32_t cores = listed != 0 ? listed : lane_core_count();
    char sources[3][PATH_MAX];
    char references[3][PATH_MAX];
    char object[PATH_MAX];
    char errors[PATH_MAX];
    char detail[192];
    snprintf(sources[0], sizeof(sources[0]), "%s/program_%u.c", directory, programs);
    snprintf(object, sizeof(object), "%s/growth.o", directory);
    snprintf(errors, sizeof(errors), "%s/growth.errors", directory);
    const uint64_t capacity = UINT64_C(1) << 24;
    char* text = malloc(capacity);
    bool referenced = text != NULL;
    for (uint32_t p = 0; referenced && p < 3; p += 1)
    {
        snprintf(references[p], sizeof(references[p]), "%s/growth_reference_%u.o", directory, p);
        if (p != 0)
        {
            snprintf(sources[p], sizeof(sources[p]), "%s/growth_%u.c", directory, sizes[p]);
            const uint64_t length = check_lanes_test_generate(tally, text, capacity, sizes[p]);
            FILE* file = fopen(sources[p], "wb");
            const bool written = file != NULL && fwrite(text, 1, length, file) == length;
            referenced = file != NULL && fclose(file) == 0 && written;
        }
        char* reference_arguments[] = { (char*)executable, (char*)"--lanes", (char*)"1", (char*)"-c", (char*)"-o", references[p], sources[p], NULL };
        referenced = referenced && check_lanes_test_run(reference_arguments, errors) == 0;
        check_lanes_test_expect(tally, referenced, "the growing team's reference object on one lane", sources[p]);
    }
    free(text);
    for (uint32_t c = 0; referenced && cpus > 1 && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const CheckLanesGrowthCase* growth = cases + c;
        int fds[2] = { -1, -1 };
        uint8_t tokens[16];
        for (uint32_t i = 0; i < growth->initial; i += 1)
        {
            tokens[i] = (uint8_t)(0x10 + i);
        }
        const bool ready = pipe(fds) == 0 && (growth->initial == 0 || write(fds[1], tokens, growth->initial) == (ssize_t)growth->initial);
        char makeflags[96];
        char supply[64];
        snprintf(makeflags, sizeof(makeflags), "MAKEFLAGS=-j --jobserver-auth=%d,%d", fds[0], fds[1]);
        snprintf(supply, sizeof(supply), "BUSTER_TEST_WORKER_SUPPLY=%u", growth->supply);
        unlink(object);
        char* arguments[14] = { (char*)"env", makeflags, supply, (char*)executable };
        uint32_t count = 4;
        for (uint32_t o = 0; o < 2 && growth->options[o] != NULL; o += 1)
        {
            arguments[count++] = (char*)growth->options[o];
        }
        const char* tail[] = { "-c", "--time-stages", "-o", object, sources[growth->program] };
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(tail); t += 1)
        {
            arguments[count++] = (char*)tail[t];
        }
        arguments[count] = NULL;
        const int status = ready ? check_lanes_test_run(arguments, errors) : -1;
        uint32_t reported[6] = { 0 };
        const bool found = check_lanes_test_report_budget(errors, reported);
        // The lanes the team may take: those the lane rule asked for (`requested`), up to the cap. It starts on a lane
        // per spare token, its own first; only a team short of them grows, and supplies, and it grows where its parts
        // pay, by the tokens supplied, up to them.
        const bool every_lane = growth->options[0] != NULL && strcmp(growth->options[0], "--lanes") == 0;
        const uint32_t cap = growth->options[0] != NULL && !every_lane ? 3 : 64;
        const uint32_t room = reported[1] < cap ? reported[1] : cap;
        const uint32_t granted = 1 + (room == 0 ? 0 : growth->initial < room - 1 ? growth->initial : room - 1);
        const uint32_t supplied = granted < room ? growth->supply : 0;
        const bool pays = every_lane || growth->program == 1;
        const uint32_t active = !pays ? granted : granted + supplied < room ? granted + supplied : room;
        for (uint32_t i = 0; i < supplied; i += 1)
        {
            tokens[growth->initial + i] = (uint8_t)(BUSTER_CHECK_EMIT_SUPPLY_BYTE + i);
        }
        snprintf(detail, sizeof(detail), "case %u: %u tokens, %u supplied: status %d, requested %u, granted %u, active %u, grown %u, worked %u", c,
                 growth->initial, growth->supply, status, reported[1], reported[2], reported[3], reported[4], reported[5]);
        check_lanes_test_expect(tally, status == 0 && check_test_driver_same_files(object, references[growth->program]), "a growing team's object is --lanes 1's",
                                detail);
        check_lanes_test_expect(tally, found && (!every_lane || reported[1] == (cpus < 4 ? cpus : 4)) && (growth->program == 0 || cores < 2 || reported[1] > 1),
                                "the growing team's request is the lane rule's", detail);
        check_lanes_test_expect(tally, found && reported[2] == granted && reported[3] == active && reported[4] == active - granted,
                                pays ? "a team short of its lanes grows by the tokens that come back, up to its lanes and the cap" :
                                       "a team whose parts left don't pay for a guardian doesn't grow",
                                detail);
        check_lanes_test_expect(tally, found && reported[5] >= 1 && reported[5] <= active, "no lane of a growing team takes a part before it is admitted",
                                detail);
        check_lanes_test_expect(tally, ready && worker_budget_test_tokens(fds[0], tokens, growth->initial + supplied),
                                "a growing team returns every token it took once, its growths' too", detail);
        if (fds[0] >= 0)
        {
            close(fds[0]);
        }
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
    }
}

// The driver on each failing program `failure_N.c` in `directory` (see the file's comment), by each path: by --lanes 8,
// by --lanes 8 with the identifiers' ids in another order (from `id_order` on), and by --lanes 8 in a child whose lanes
// don't start (not as root), the exit status and diagnostics --lanes 1 gives.
static void check_lanes_test_driver_failing(CheckLanesTestTally* tally, const char* executable, const char* directory, uint64_t id_order)
{
    static const char* const ways[] = { "1", "8", "8 with the ids in another order", "8 that don't start" };
    char detail[PATH_MAX + 128];
    uint32_t compared = 0;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_lanes_test_failures); f += 1)
    {
        char source[PATH_MAX];
        char object[PATH_MAX];
        snprintf(source, sizeof(source), "%s/failure_%u.c", directory, f);
        snprintf(object, sizeof(object), "%s/failure_driver.o", directory);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char errors[BUSTER_ARRAY_LENGTH(ways)][PATH_MAX];
            int first = -1;
            for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(ways) && (w + 1 < BUSTER_ARRAY_LENGTH(ways) || geteuid() != 0); w += 1)
            {
                snprintf(errors[w], sizeof(errors[w]), "%s/failure_%u_%u_%u.errors", directory, f, scalar, w);
                char* arguments[10] = { (char*)executable };
                uint32_t count = 1;
                if (scalar != 0)
                {
                    arguments[count++] = (char*)"--scalar";
                }
                arguments[count++] = w == 0 ? (char*)"--lanes=1" : (char*)"--lanes=8";
                char order[48];
                snprintf(order, sizeof(order), "--id-order=%llu", (unsigned long long)(id_order + f));
                if (w == 2)
                {
                    arguments[count++] = order;
                }
                arguments[count++] = (char*)"-c";
                arguments[count++] = (char*)"-o";
                arguments[count++] = object;
                arguments[count] = source;
                const int status = w + 1 == BUSTER_ARRAY_LENGTH(ways) ? check_lanes_test_run_unstarted(arguments, errors[w]) : check_lanes_test_run(arguments, errors[w]);
                snprintf(detail, sizeof(detail), "%s, %s path, --lanes %s: exit status %d", check_lanes_test_failures[f].name, scalar != 0 ? "scalar" : "vector", ways[w],
                         status);
                struct stat errors_stat;
                if (w == 0)
                {
                    first = status;
                    check_lanes_test_expect(tally, status == 1 && stat(errors[0], &errors_stat) == 0 && errors_stat.st_size > 0,
                                            "the driver refuses a failing program with a diagnostic", detail);
                }
                else
                {
                    check_lanes_test_expect(tally, status == first && check_test_driver_same_files(errors[w], errors[0]), "the driver's diagnostic on lanes is one lane's",
                                            detail);
                    compared += 1;
                }
            }
        }
    }
    printf("  driver: %u failing programs' diagnostics on lanes and with the ids in another order, each one lane's\n", compared);
}

// The deferred reader's lines of a --time-stages report at `path` (CheckClock.deferred): the lanes its bodies took into
// `lanes` and the deferred-merged value into `merged` ("yes", or why the unit went to source order); false if it has none.
static bool check_lanes_test_report_deferred(const char* path, uint32_t* lanes, char* merged, uint32_t capacity)
{
    FILE* file = fopen(path, "r");
    char line[256];
    bool found = false;
    *lanes = 0;
    merged[0] = 0;
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        unsigned value = 0;
        if (sscanf(line, "time-check\tdeferred-lanes\t%u", &value) == 1)
        {
            *lanes = value;
            found = true;
        }
        else if (strncmp(line, "time-check\tdeferred-merged\t", 27) == 0)
        {
            snprintf(merged, capacity, "%s", line + 27);
            merged[strcspn(merged, "\n")] = 0;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return found && merged[0] != 0;
}

// #151's deferred bodies in the driver (--deferred-bodies, check_input_deferred_compile): the checker reads a unit's
// bodies apart, on the command's lanes, as rounds of its flow. The four generated programs of check_lanes_test_driver by
// the vector path, by --deferred-bodies with --lanes 1, 2, 3 and 8, by default, and by --lanes 8 with the ids in another
// order: their objects and executables source order's on one lane (check_lanes_test_driver's --lanes 1), byte for byte,
// and the executables exit alike. With --time-stages, the program of 600 functions on 8 lanes reads its bodies on more than one
// lane (with more than one CPU) and merges them, on 1 lane goes to source order, and after a `#pragma clang section` line
// goes to source order before its top level's walk; under a jobserver's spare tokens and a worker budget, its bodies
// take the lanes they give, and every token goes back; a unit's 16-byte lock-free queries answer by -mcx16 as in source
// order; in a child whose lanes don't start, by --lanes 8, its object is source order's. Each failing program by --deferred-bodies with --lanes 2, 3 and 8,
// and in such a child: --lanes 1's exit status and diagnostics. And --no-deferred-bodies after it reads in source order.
static void check_lanes_test_driver_deferred(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs, uint64_t id_order)
{
    static const char* const ways[] = { "--lanes=1", "--lanes=2", "--lanes=3", "--lanes=8", NULL, "--lanes=8" };
    const uint32_t chosen[] = { 2, 3, 5, programs };
    char detail[PATH_MAX + 128];
    uint32_t compared = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(chosen) && chosen[i] < programs + 2; i += 1)
    {
        const uint32_t p = chosen[i];
        char source[PATH_MAX];
        char errors[PATH_MAX];
        char first_object[PATH_MAX];
        char first_program[PATH_MAX];
        snprintf(source, sizeof(source), "%s/program_%u.c", directory, p);
        snprintf(errors, sizeof(errors), "%s/deferred_%u.errors", directory, p);
        snprintf(first_object, sizeof(first_object), "%s/driver_%u_0_0.o", directory, p);
        snprintf(first_program, sizeof(first_program), "%s/driver_%u_0_0", directory, p);
        char* first_run[] = { first_program, NULL };
        const int first_status = check_lanes_test_run(first_run, "/dev/null");
        for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(ways); w += 1)
        {
            char object[PATH_MAX];
            char program[PATH_MAX];
            char order[48];
            snprintf(object, sizeof(object), "%s/deferred_%u_%u.o", directory, p, w);
            snprintf(program, sizeof(program), "%s/deferred_%u_%u", directory, p, w);
            snprintf(order, sizeof(order), "--id-order=%llu", (unsigned long long)(id_order + p));
            char* object_arguments[12] = { (char*)executable, (char*)"--deferred-bodies" };
            char* program_arguments[12] = { (char*)executable, (char*)"--deferred-bodies" };
            uint32_t count = 2;
            if (ways[w] != NULL)
            {
                object_arguments[count] = (char*)ways[w];
                program_arguments[count++] = (char*)ways[w];
            }
            if (w + 1 == BUSTER_ARRAY_LENGTH(ways))
            {
                object_arguments[count] = order;
                program_arguments[count++] = order;
            }
            object_arguments[count] = (char*)"-c";
            program_arguments[count] = (char*)"-o";
            object_arguments[count + 1] = (char*)"-o";
            program_arguments[count + 1] = program;
            object_arguments[count + 2] = object;
            program_arguments[count + 2] = source;
            object_arguments[count + 3] = source;
            const bool objected = check_lanes_test_run(object_arguments, errors) == 0;
            const bool linked = check_lanes_test_run(program_arguments, errors) == 0;
            char* run_arguments[] = { program, NULL };
            const int status = linked ? check_lanes_test_run(run_arguments, "/dev/null") : -1;
            snprintf(detail, sizeof(detail), "%s, --deferred-bodies %s%s%s", source, ways[w] != NULL ? ways[w] : "by default",
                     w + 1 == BUSTER_ARRAY_LENGTH(ways) ? " " : "", w + 1 == BUSTER_ARRAY_LENGTH(ways) ? order : "");
            check_lanes_test_expect(tally, objected && check_test_driver_same_files(object, first_object), "the driver's object with deferred bodies is source order's",
                                    detail);
            check_lanes_test_expect(tally, linked && check_test_driver_same_files(program, first_program) && status == first_status && first_status >= 0,
                                    "the driver's executable with deferred bodies is source order's, and exits as it does", detail);
            compared += 1;
        }
    }
    // The program of 600 functions: its bodies on lanes and merged, by --lanes 8, and on one lane in source order.
    char source[PATH_MAX];
    char object[PATH_MAX];
    char report[PATH_MAX];
    char first_object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/program_%u.c", directory, programs);
    snprintf(object, sizeof(object), "%s/deferred_report.o", directory);
    snprintf(report, sizeof(report), "%s/deferred_report.errors", directory);
    snprintf(first_object, sizeof(first_object), "%s/driver_%u_0_0.o", directory, programs);
    const uint32_t cpus = check_driver_cpus();
    static const char* const reported[] = { "--lanes=8", "--lanes=1" };
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(reported); r += 1)
    {
        char* arguments[] = { (char*)executable, (char*)"--deferred-bodies", (char*)reported[r], (char*)"--time-stages", (char*)"-c", (char*)"-o", object, source, NULL };
        const bool objected = check_lanes_test_run(arguments, report) == 0;
        uint32_t lanes = 0;
        char merged[128];
        const bool found = check_lanes_test_report_deferred(report, &lanes, merged, sizeof(merged));
        const bool laned = r == 0 && cpus > 1;
        snprintf(detail, sizeof(detail), "%s, --deferred-bodies %s on %u CPUs: %u lanes, merged: %s", source, reported[r], cpus, lanes, found ? merged : "(no report)");
        check_lanes_test_expect(tally,
                                objected && found && check_test_driver_same_files(object, first_object) &&
                                    (laned ? lanes > 1 && strcmp(merged, "yes") == 0 : lanes == 0 && strcmp(merged, "one lane") == 0),
                                laned ? "the driver reads the bodies on lanes and merges them" : "the driver reads a unit on one lane in source order", detail);
    }
    // A unit with a `#pragma clang section` line, which the deferred reader leaves to source order before its top level's
    // walk (check_deferred_sections): the program of 600 functions after one, source order's object.
    char sectioned[PATH_MAX];
    char sectioned_object[PATH_MAX];
    snprintf(sectioned, sizeof(sectioned), "%s/deferred_section.c", directory);
    snprintf(sectioned_object, sizeof(sectioned_object), "%s/deferred_section_source_order.o", directory);
    FILE* section_file = fopen(sectioned, "w");
    const bool section_written = section_file != NULL && fprintf(section_file, "#pragma clang section text = \".text.lanes\"\n#include \"%s\"\n", source) > 0;
    if (section_file != NULL)
    {
        fclose(section_file);
    }
    char* section_order[] = { (char*)executable, (char*)"--lanes=8", (char*)"-c", (char*)"-o", sectioned_object, sectioned, NULL };
    char* section_deferred[] = { (char*)executable, (char*)"--deferred-bodies", (char*)"--lanes=8", (char*)"--time-stages", (char*)"-c", (char*)"-o", object, sectioned, NULL };
    uint32_t section_lanes = 0;
    char section_merged[128] = { 0 };
    const bool section_compiled = section_written && check_lanes_test_run(section_order, report) == 0 && check_lanes_test_run(section_deferred, report) == 0;
    const bool section_reported = section_compiled && check_lanes_test_report_deferred(report, &section_lanes, section_merged, sizeof(section_merged));
    snprintf(detail, sizeof(detail), "%s: %u lanes, merged: %s", sectioned, section_lanes, section_reported ? section_merged : "(no report)");
    check_lanes_test_expect(tally,
                            section_reported && section_lanes == 0 && strcmp(section_merged, "#pragma clang section") == 0 &&
                                check_test_driver_same_files(object, sectioned_object),
                            "the driver reads a unit with #pragma clang section in source order, before its top level's walk", detail);
    // The target's 16-byte atomics (cx16), which the lock-free queries read from the caller's table after the merge
    // (check_input_target_table, #1403's review): a unit whose eight bodies ask for 16 bytes' lock-freedom, by -mcx16 on
    // 8 lanes, source order's object; and by -mno-cx16 another, so that the queries' answers reach the object.
    char atomics[PATH_MAX];
    char atomics_order[PATH_MAX];
    char atomics_narrow[PATH_MAX];
    snprintf(atomics, sizeof(atomics), "%s/deferred_cx16.c", directory);
    snprintf(atomics_order, sizeof(atomics_order), "%s/deferred_cx16_source_order.o", directory);
    snprintf(atomics_narrow, sizeof(atomics_narrow), "%s/deferred_cx16_narrow.o", directory);
    FILE* atomics_file = fopen(atomics, "w");
    bool atomics_written = atomics_file != NULL;
    for (uint32_t f = 0; atomics_written && f < 8; f += 1)
    {
        atomics_written = fprintf(atomics_file,
                                  "int q%u(int x) { int y = x * %u; for (int i = 0; i < x; i += 1) { y += i; } "
                                  "return y + __atomic_always_lock_free(16, 0) * 2 + __atomic_is_lock_free(16, 0); }\n",
                                  f, f) > 0;
    }
    if (atomics_file != NULL)
    {
        atomics_written = fclose(atomics_file) == 0 && atomics_written;
    }
    char* atomics_source_order[] = { (char*)executable, (char*)"--lanes=8", (char*)"-mcx16", (char*)"-c", (char*)"-o", atomics_order, atomics, NULL };
    char* atomics_deferred[] = { (char*)executable, (char*)"--deferred-bodies", (char*)"--lanes=8", (char*)"--time-stages", (char*)"-mcx16", (char*)"-c", (char*)"-o",
                                 object, atomics, NULL };
    char* atomics_without[] = { (char*)executable, (char*)"--lanes=8", (char*)"-mno-cx16", (char*)"-c", (char*)"-o", atomics_narrow, atomics, NULL };
    uint32_t atomics_lanes = 0;
    char atomics_merged[128] = { 0 };
    const bool atomics_compiled = atomics_written && check_lanes_test_run(atomics_source_order, report) == 0 && check_lanes_test_run(atomics_without, report) == 0 &&
                                  check_lanes_test_run(atomics_deferred, report) == 0;
    const bool atomics_reported = atomics_compiled && check_lanes_test_report_deferred(report, &atomics_lanes, atomics_merged, sizeof(atomics_merged));
    snprintf(detail, sizeof(detail), "%s by -mcx16: %u lanes, merged: %s", atomics, atomics_lanes, atomics_reported ? atomics_merged : "(no report)");
    check_lanes_test_expect(tally,
                            atomics_reported && (cpus < 2 || (atomics_lanes > 1 && strcmp(atomics_merged, "yes") == 0)) &&
                                check_test_driver_same_files(object, atomics_order) && !check_test_driver_same_files(atomics_order, atomics_narrow),
                            "the driver's deferred bodies answer 16-byte lock-free queries by the target's cx16, as source order does", detail);
    // The worker budget (#673): under a jobserver pipe of none, 1, 3 and 7 spare tokens, by --lanes 8, the bodies' grant
    // takes a lane for each token it finds, without waiting, up to the CPUs (none: source order); every token is back in the
    // pipe once as the command returns; and by --worker-budget 1 and 3 alone, no more lanes than the budget.
    static const uint32_t spare[] = { 0, 1, 3, 7 };
    const uint32_t most = cpus < 8 ? cpus : 8;
    for (uint32_t k = 0; cpus > 1 && k < BUSTER_ARRAY_LENGTH(spare); k += 1)
    {
        int fds[2] = { -1, -1 };
        uint8_t tokens[8];
        for (uint32_t i = 0; i < spare[k]; i += 1)
        {
            tokens[i] = (uint8_t)(0x30 + i);
        }
        const bool ready = pipe(fds) == 0 && (spare[k] == 0 || write(fds[1], tokens, spare[k]) == (ssize_t)spare[k]);
        char makeflags[96];
        snprintf(makeflags, sizeof(makeflags), "MAKEFLAGS=-j --jobserver-auth=%d,%d", fds[0], fds[1]);
        char* arguments[] = { (char*)"env", makeflags, (char*)executable, (char*)"--deferred-bodies", (char*)"--lanes=8", (char*)"--time-stages", (char*)"-c", (char*)"-o",
                              object, source, NULL };
        const int status = ready ? check_lanes_test_run(arguments, report) : -1;
        uint32_t lanes = 0;
        char merged[128] = { 0 };
        const bool found = status == 0 && check_lanes_test_report_deferred(report, &lanes, merged, sizeof(merged));
        const uint32_t expected = 1 + spare[k] < most ? 1 + spare[k] : most;
        snprintf(detail, sizeof(detail), "%s, %u spare tokens on %u CPUs: status %d, %u lanes, merged: %s", source, spare[k], cpus, status, lanes,
                 found ? merged : "(no report)");
        check_lanes_test_expect(tally,
                                found && check_test_driver_same_files(object, first_object) &&
                                    (expected > 1 ? lanes == expected && strcmp(merged, "yes") == 0 : lanes == 0 && strcmp(merged, "too few lanes") == 0),
                                "the deferred bodies take a lane for each spare token, source order's object", detail);
        check_lanes_test_expect(tally, ready && worker_budget_test_tokens(fds[0], tokens, spare[k]), "the deferred bodies return every token they took once",
                                detail);
        if (fds[0] >= 0)
        {
            close(fds[0]);
        }
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
    }
    static const char* const budgets[] = { "--worker-budget=1", "--worker-budget=3" };
    static const uint32_t budget_lanes[] = { 1, 3 };
    for (uint32_t b = 0; cpus > 1 && b < BUSTER_ARRAY_LENGTH(budgets); b += 1)
    {
        char* arguments[] = { (char*)"env", (char*)"MAKEFLAGS=", (char*)executable, (char*)"--deferred-bodies", (char*)"--lanes=8", (char*)budgets[b], (char*)"--time-stages",
                              (char*)"-c", (char*)"-o", object, source, NULL };
        const int status = check_lanes_test_run(arguments, report);
        uint32_t lanes = 0;
        char merged[128] = { 0 };
        const bool found = status == 0 && check_lanes_test_report_deferred(report, &lanes, merged, sizeof(merged));
        const uint32_t expected = budget_lanes[b] < most ? budget_lanes[b] : most;
        snprintf(detail, sizeof(detail), "%s, %s on %u CPUs: status %d, %u lanes, merged: %s", source, budgets[b], cpus, status, lanes, found ? merged : "(no report)");
        check_lanes_test_expect(tally,
                                found && check_test_driver_same_files(object, first_object) &&
                                    (expected > 1 ? lanes == expected && strcmp(merged, "yes") == 0 : lanes == 0 && strcmp(merged, "one lane") == 0),
                                "the deferred bodies take no more lanes than the worker budget, source order's object", detail);
    }
    // --no-deferred-bodies after --deferred-bodies: source order's reader, which reports nothing of the deferred one.
    char* undone[] = { (char*)executable, (char*)"--deferred-bodies", (char*)"--no-deferred-bodies", (char*)"--lanes=8", (char*)"--time-stages", (char*)"-c", (char*)"-o",
                       object, source, NULL };
    uint32_t undone_lanes = 0;
    char undone_merged[128];
    const bool undone_written = check_lanes_test_run(undone, report) == 0;
    check_lanes_test_expect(tally,
                            undone_written && !check_lanes_test_report_deferred(report, &undone_lanes, undone_merged, sizeof(undone_merged)) &&
                                check_test_driver_same_files(object, first_object),
                            "--no-deferred-bodies reads the unit in source order", source);
    uint32_t unstarted = 0;
    if (geteuid() != 0)
    {
        char* arguments[] = { (char*)executable, (char*)"--deferred-bodies", (char*)"--lanes=8", (char*)"-c", (char*)"-o", object, source, NULL };
        check_lanes_test_expect(tally, check_lanes_test_run_unstarted(arguments, report) == 0 && check_test_driver_same_files(object, first_object),
                                "the driver's object with deferred bodies whose lanes don't start is source order's", source);
        unstarted += 1;
    }
    // The failing programs: --lanes 1's exit status and diagnostics (check_lanes_test_driver_failing's, by the vector
    // path), on lanes and in a child whose lanes don't start.
    static const char* const failing_ways[] = { "--lanes=2", "--lanes=3", "--lanes=8", "--lanes=8" };
    uint32_t failures = 0;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_lanes_test_failures); f += 1)
    {
        char failing[PATH_MAX];
        char first_errors[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(failing, sizeof(failing), "%s/failure_%u.c", directory, f);
        snprintf(first_errors, sizeof(first_errors), "%s/failure_%u_0_0.errors", directory, f);
        snprintf(errors, sizeof(errors), "%s/failure_%u_deferred.errors", directory, f);
        snprintf(object, sizeof(object), "%s/failure_deferred.o", directory);
        for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(failing_ways) && (w + 1 < BUSTER_ARRAY_LENGTH(failing_ways) || geteuid() != 0); w += 1)
        {
            char* arguments[] = { (char*)executable, (char*)"--deferred-bodies", (char*)failing_ways[w], (char*)"-c", (char*)"-o", object, failing, NULL };
            const bool alone = w + 1 == BUSTER_ARRAY_LENGTH(failing_ways);
            const int status = alone ? check_lanes_test_run_unstarted(arguments, errors) : check_lanes_test_run(arguments, errors);
            snprintf(detail, sizeof(detail), "%s, --deferred-bodies %s%s: exit status %d", check_lanes_test_failures[f].name, failing_ways[w],
                     alone ? " that don't start" : "", status);
            check_lanes_test_expect(tally, status == 1 && check_test_driver_same_files(errors, first_errors), "the driver's diagnostic with deferred bodies is one lane's",
                                    detail);
            failures += 1;
        }
    }
    printf("  driver: %u objects and executables with deferred bodies, %u whose lanes didn't start, each source order's, and %u failing programs' "
           "diagnostics, each one lane's\n",
           compared, unstarted, failures);
}

// The benchmark's untimed object warm-ups must place named sections as -c does, and reject a section -c refuses.
// Timed runs still measure only the function emission. One pass and two requested lanes are sufficient for these
// acceptance controls; no timing is judged here. The benchmark command is only present in the vector host build.
static void check_lanes_test_benchmark_sections(CheckLanesTestTally* tally, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        int status;
    } fixtures[] = {
        { "int f(void) __attribute__((section(\".text.x\")));\nint f(void) { return 17; }\nint main(void) { return f() - 17; }\n", 0 },
        { "__asm__(\".section .data.x,\\\"aw\\\",@progbits\\n.byte 11\\n.text\");\nint main(void) { return 0; }\n", 0 },
        { "int f(void) __attribute__((section(\".note.x\")));\nint f(void) { return 17; }\nint main(void) { return f() - 17; }\n", 1 },
    };
    for (uint32_t i = 0; BUSTER_HOST_VECTOR && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(source, sizeof(source), "%s/benchmark_section_%u.c", directory, i);
        snprintf(errors, sizeof(errors), "%s/benchmark_section_%u.errors", directory, i);
        const bool wrote = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)executable, (char*)"benchmark-check-lanes", (char*)"--passes", (char*)"1", (char*)"--lanes", (char*)"1,2", source, NULL };
        const int status = wrote ? check_lanes_test_run(arguments, errors) : -1;
        check_lanes_test_expect(tally, status == fixtures[i].status, "the lane benchmark keeps -c's section acceptance", source);
    }
}

// The lanes --time-stages' report in the file at `path` says its command asked for and ran on, each 0 where it has none.
static void check_lanes_test_report_lanes(const char* path, uint32_t* asked, uint32_t* lanes)
{
    *asked = 0;
    *lanes = 0;
    FILE* file = fopen(path, "r");
    char line[256];
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        unsigned value = 0;
        if (sscanf(line, "time-process\tlanes-asked\t%u", &value) == 1)
        {
            *asked = value;
        }
        else if (sscanf(line, "time-process\tlanes\t%u", &value) == 1)
        {
            *lanes = value;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
}

// The driver's session's threads (see the file's comment): `executable`'s calls of pthread_create, counted by a library
// preloaded into it, for commands on `directory`'s generated programs (the first of 600 functions, the `programs`th, and
// the small one of three), each command's object compared with --lanes 1's.
static void check_lanes_test_session_threads(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs)
{
    static const char counter[] = "#define _GNU_SOURCE\n"
                                  "#include <dlfcn.h>\n"
                                  "#include <pthread.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <stdlib.h>\n"
                                  "typedef int (*Create)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);\n"
                                  "static unsigned created;\n"
                                  "int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*start)(void*), void* argument)\n"
                                  "{\n"
                                  "    Create create = (Create)dlsym(RTLD_NEXT, \"pthread_create\");\n"
                                  "    __atomic_add_fetch(&created, 1, __ATOMIC_RELAXED);\n"
                                  "    return create != 0 ? create(thread, attributes, start, argument) : 11;\n"
                                  "}\n"
                                  "__attribute__((destructor)) static void report(void)\n"
                                  "{\n"
                                  "    const char* path = getenv(\"CHECK_LANES_THREADS\");\n"
                                  "    FILE* file = path != 0 ? fopen(path, \"w\") : 0;\n"
                                  "    if (file != 0)\n"
                                  "    {\n"
                                  "        fprintf(file, \"%u\\n\", created);\n"
                                  "        fclose(file);\n"
                                  "    }\n"
                                  "}\n";
    char source[PATH_MAX];
    char library[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(source, sizeof(source), "%s/session_threads.c", directory);
    snprintf(library, sizeof(library), "%s/session_threads.so", directory);
    snprintf(errors, sizeof(errors), "%s/session_threads.errors", directory);
    char* compile[] = { (char*)"cc", (char*)"-shared", (char*)"-fPIC", (char*)"-O1", source, (char*)"-o", library, NULL };
    FILE* file = fopen(source, "w");
    const bool written = file != NULL && fwrite(counter, 1, sizeof(counter) - 1, file) == sizeof(counter) - 1;
    const bool closed = file != NULL && fclose(file) == 0;
    const bool built = written && closed && object_test_run(compile, "/dev/null", errors) == 0;
    check_lanes_test_expect(tally, built, "compile the preloaded counter of pthread_create", source);
    // A program large enough for its session to start its lanes, by default once it is preprocessed (#1444), and one too
    // small for them, whose session creates no thread by default; each with --lanes 1's object.
    char program[PATH_MAX];
    char second[PATH_MAX];
    char reference[PATH_MAX];
    char small_reference[PATH_MAX];
    snprintf(program, sizeof(program), "%s/program_%u.c", directory, programs);
    snprintf(second, sizeof(second), "%s/program_2.c", directory);
    snprintf(reference, sizeof(reference), "%s/session_threads_reference.o", directory);
    snprintf(small_reference, sizeof(small_reference), "%s/session_threads_small_reference.o", directory);
    char* reference_arguments[] = { (char*)executable, (char*)"--lanes", (char*)"1", (char*)"-c", (char*)"-o", reference, program, NULL };
    char* small_arguments[] = { (char*)executable, (char*)"--lanes", (char*)"1", (char*)"-c", (char*)"-o", small_reference, second, NULL };
    const bool referenced = built && check_lanes_test_run(reference_arguments, errors) == 0 && check_lanes_test_run(small_arguments, errors) == 0;
    check_lanes_test_expect(tally, !built || referenced, "the driver writes the reference objects on one lane", program);
    const uint32_t cpus = check_driver_cpus();
    const uint32_t two_cpus = cpus < 2 ? cpus : 2;
    const uint32_t three_cpus = cpus < 3 ? cpus : 3;
    const uint32_t four_cpus = cpus < 4 ? cpus : 4;
    // By default one lane per core of those CPUs, by /proc/cpuinfo (lane_test_process_cores), or where it doesn't list
    // them by sysfs (lane_core_count, test-lanes' to check): its session's lane 1, created from 2 lanes on, counts them,
    // and runs alone where they leave it no other lane.
    const uint32_t listed = lane_test_process_cores();
    const uint32_t cores = listed != 0 ? listed : lane_core_count();
    const uint32_t per_core = cores < cpus ? cores : cpus;
    const uint32_t per_core_threads = cpus < 2 ? 0 : per_core < 2 ? 1 : per_core - 1;
    // A core's two siblings and CPUs of two cores, where the process has them.
    uint64_t siblings[BUSTER_LANE_CPU_MASK_WORDS];
    uint64_t apart[BUSTER_LANE_CPU_MASK_WORDS];
    const bool paired = lane_test_sibling_masks(siblings, apart);
    // The commands: their options, their sources (the large program, the small one, or the large and the small), whether
    // they write an object to compare, the lane of the session that fails to start (0 for none), an affinity cap (0 for
    // unchanged) or the CPUs to run on, the threads they create, and with --time-stages the lanes its report must say the
    // session asked for and ran on.
    typedef struct CheckLanesSessionCommand CheckLanesSessionCommand;
    struct CheckLanesSessionCommand
    {
        const char* options[4];
        bool object;
        bool link;
        bool small;
        bool two;
        const char* fail_at;
        uint32_t cpu_limit;
        const uint64_t* mask;
        uint32_t threads;
        uint32_t asked;
        uint32_t ran;
    };
    CheckLanesSessionCommand commands[20] = {
        { .options = { "--lanes", "1", "-c" }, .object = true, .threads = 0 },
        { .options = { "--worker-budget=1", "-c" }, .object = true, .threads = 0 },
        { .options = { "-fsyntax-only" }, .threads = 0 },
        { .options = { "--lanes", "3", "-c", "--time-stages" }, .object = true, .threads = three_cpus - 1, .asked = three_cpus, .ran = three_cpus },
        { .options = { "-c", "--time-stages" }, .object = true, .threads = per_core_threads, .asked = per_core, .ran = per_core },
        { .options = { "-c" }, .object = true, .cpu_limit = 1, .threads = 0 },
        { .options = { "--lanes=3", "-c", "--time-stages" }, .object = true, .cpu_limit = 1, .threads = 0, .asked = 1, .ran = 1 },
        { .options = { "--lanes=8", "--worker-budget=3", "-c", "--time-stages" }, .object = true, .cpu_limit = 2, .threads = two_cpus - 1, .asked = two_cpus, .ran = two_cpus },
        { .options = { "--lanes=8", "--worker-budget=1", "-c", "--time-stages" }, .object = true, .cpu_limit = 2, .threads = 0, .asked = 1, .ran = 1 },
        { .options = { "-o" }, .link = true, .threads = per_core_threads },
        { .options = { "-c" }, .two = true, .threads = per_core_threads },
        { .options = { "--lanes", "4", "-c", "--time-stages" }, .object = true, .fail_at = "2", .threads = cpus < 3 ? cpus - 1 : 1, .asked = four_cpus, .ran = cpus < 3 ? cpus : 1 },
        // The small program by default, whose session starts no lane (#1444): its object and its executable.
        { .options = { "-c", "--time-stages" }, .object = true, .small = true, .threads = 0, .asked = 1, .ran = 1 },
        { .options = { "-o" }, .link = true, .small = true, .threads = 0 },
    };
    uint32_t command_count = 14;
    if (paired)
    {
        // On a core's two siblings, by default one lane, its lane 1 created to count the core; on CPUs of two cores, two;
        // and --lanes 2, per CPU, two on the siblings.
        commands[command_count++] =
            (CheckLanesSessionCommand){ .options = { "-c", "--time-stages" }, .object = true, .mask = siblings, .threads = 1, .asked = 1, .ran = 1 };
        commands[command_count++] =
            (CheckLanesSessionCommand){ .options = { "-c", "--time-stages" }, .object = true, .mask = apart, .threads = 1, .asked = 2, .ran = 2 };
        commands[command_count++] =
            (CheckLanesSessionCommand){ .options = { "--lanes", "2", "-c", "--time-stages" }, .object = true, .mask = siblings, .threads = 1, .asked = 2, .ran = 2 };
    }
    uint64_t inherited = 0;
    for (char** entry = environ; entry != NULL && *entry != NULL; entry += 1)
    {
        inherited += 1;
    }
    char** environment = calloc(inherited + 5, sizeof(char*));
    char preload[PATH_MAX + 16];
    char count_path[PATH_MAX + 32];
    char lanes_path[PATH_MAX + 32];
    char failing[64];
    for (uint32_t c = 0; referenced && environment != NULL && c < command_count; c += 1)
    {
        const CheckLanesSessionCommand* command = commands + c;
        char object[PATH_MAX];
        char counted[PATH_MAX];
        char lanes_counted[PATH_MAX];
        char executable_path[PATH_MAX];
        snprintf(object, sizeof(object), "%s/session_threads_%u.o", directory, c);
        snprintf(counted, sizeof(counted), "%s/session_threads_%u.count", directory, c);
        snprintf(lanes_counted, sizeof(lanes_counted), "%s/session_threads_%u.lanes", directory, c);
        snprintf(executable_path, sizeof(executable_path), "%s/session_threads_%u", directory, c);
        snprintf(preload, sizeof(preload), "LD_PRELOAD=%s", library);
        snprintf(count_path, sizeof(count_path), "CHECK_LANES_THREADS=%s", counted);
        snprintf(lanes_path, sizeof(lanes_path), "BUSTER_TEST_THREADS=%s", lanes_counted);
        snprintf(failing, sizeof(failing), "BUSTER_TEST_LANE_FAIL_AT=%s", command->fail_at != NULL ? command->fail_at : "");
        uint64_t used = 0;
        for (uint64_t i = 0; i < inherited; i += 1)
        {
            if (strncmp(environ[i], "LD_PRELOAD=", 11) != 0 && strncmp(environ[i], "CHECK_LANES_THREADS=", 20) != 0 &&
                strncmp(environ[i], "BUSTER_TEST_LANE_FAIL_AT=", 25) != 0 && strncmp(environ[i], "BUSTER_TEST_THREADS=", 20) != 0)
            {
                environment[used] = environ[i];
                used += 1;
            }
        }
        // The preloaded counter's count, and the lanes' own (lane_threads_report), which a static executable, where
        // nothing is preloaded, gives alone.
        environment[used] = preload;
        environment[used + 1] = count_path;
        environment[used + 2] = lanes_path;
        environment[used + 3] = command->fail_at != NULL ? failing : NULL;
        environment[used + 4] = NULL;
        char* arguments[12] = { (char*)executable };
        uint32_t count = 1;
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(command->options) && command->options[o] != NULL; o += 1)
        {
            arguments[count++] = (char*)command->options[o];
        }
        if (command->object)
        {
            arguments[count++] = (char*)"-o";
            arguments[count++] = object;
        }
        if (command->link)
        {
            arguments[count++] = executable_path;
        }
        arguments[count++] = command->small ? second : program;
        if (command->two)
        {
            arguments[count++] = second;
        }
        arguments[count] = NULL;
        unlink(counted);
        unlink(lanes_counted);
        // The two sources' objects are written in the current directory, as clang names them: run them in `directory`.
        char saved[PATH_MAX];
        const bool moved = !command->two || (getcwd(saved, sizeof(saved)) != NULL && chdir(directory) == 0);
        // Limit this thread to the first allowed CPUs, or to the command's own, while it starts the command, which
        // inherits that mask, then restore the full original mask. The limited cases check explicit lanes against both
        // CPU and budget caps, and the default's one lane per core.
        const bool limit = command->cpu_limit != 0 || command->mask != NULL;
        uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
        uint64_t limited[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
        const long affinity_bytes = limit ? syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) : 0;
        uint32_t selected = 0;
        for (uint32_t cpu = 0; affinity_bytes > 0 && cpu < (uint32_t)affinity_bytes * 8 && (command->mask != NULL || selected < command->cpu_limit); cpu += 1)
        {
            const uint64_t bit = UINT64_C(1) << (cpu % 64);
            const bool chosen = command->mask != NULL ? (command->mask[cpu / 64] & bit) != 0 : (affinity[cpu / 64] & bit) != 0;
            limited[cpu / 64] |= chosen ? bit : 0;
            selected += chosen ? 1 : 0;
        }
        const bool pinned = selected != 0 && syscall(SYS_sched_setaffinity, 0, sizeof(limited), limited) == 0;
        const int status = moved && (!limit || pinned) ? object_test_run_environment(arguments, "/dev/null", errors, environment) : -1;
        const bool unpinned = !pinned || syscall(SYS_sched_setaffinity, 0, sizeof(affinity), affinity) == 0;
        const bool returned = unpinned && (!command->two || (moved && chdir(saved) == 0));
        char text[32] = { 0 };
        FILE* report = fopen(counted, "r");
        const bool read = report != NULL && fgets(text, sizeof(text), report) != NULL;
        if (report != NULL)
        {
            fclose(report);
        }
        char lanes_text[32] = { 0 };
        FILE* lanes_report = fopen(lanes_counted, "r");
        const bool lanes_read = lanes_report != NULL && fgets(lanes_text, sizeof(lanes_text), lanes_report) != NULL;
        if (lanes_report != NULL)
        {
            fclose(lanes_report);
        }
#if __has_feature(thread_sanitizer)
        // ThreadSanitizer starts a thread of its own with the process's first other one, through the same call.
        const uint32_t detector = command->threads != 0 ? 1 : 0;
#else
        const uint32_t detector = 0;
#endif
        char expected[32];
        char lanes_expected[32];
        snprintf(expected, sizeof(expected), "%u\n", command->threads + detector);
        snprintf(lanes_expected, sizeof(lanes_expected), "%u\n", command->threads);
        char detail[PATH_MAX + 128];
        snprintf(detail, sizeof(detail), "command %u: %s thread(s) preloaded, %s by the lanes, %u expected", c, read ? text : "no count, ",
                 lanes_read ? lanes_text : "no count, ", command->threads + detector);
        // The preloaded counter's, but in a static executable, and the lanes' own, which doesn't see a sanitizer's thread.
        const bool preloaded = BUSTER_HOST_STATIC_EXECUTABLE || (read && strcmp(text, expected) == 0);
        check_lanes_test_expect(tally, status == 0 && returned && preloaded && lanes_read && strcmp(lanes_text, lanes_expected) == 0,
                                "the driver's session creates its lanes once, none for one lane", detail);
        check_lanes_test_expect(tally, !command->object || check_test_driver_same_files(object, command->small ? small_reference : reference),
                                "the driver's object on its session is one lane's", detail);
        uint32_t asked = 0;
        uint32_t ran = 0;
        if (command->asked != 0)
        {
            check_lanes_test_report_lanes(errors, &asked, &ran);
        }
        snprintf(detail, sizeof(detail), "command %u: --time-stages reports %u lanes asked for and %u run on, %u and %u expected", c, asked, ran, command->asked,
                 command->ran);
        check_lanes_test_expect(tally, asked == command->asked && ran == command->ran, "--time-stages reports the lanes the session asked for and ran on",
                                detail);
    }
    free(environment);
}

// The exit helper's checks (#1444, lane_exit_helper_start): how long a run's helpers may take to end once the driver
// has, and how much of each stream a run keeps.
#define BUSTER_CHECK_LANES_EXIT_SECONDS 10
#define BUSTER_CHECK_LANES_EXIT_STREAM (UINT64_C(1) << 20)

// One run of the driver for the exit helper's checks: its status, its standard output and error, the threads its lanes
// started (BUSTER_TEST_THREADS), the other processes that became this process's children through it (the helpers, this
// process being a subreaper for the run) and how they ended, and whether both streams ended while the driver still ran.
typedef struct CheckLanesExitRun CheckLanesExitRun;
struct CheckLanesExitRun
{
    int status;
    char output[BUSTER_CHECK_LANES_EXIT_STREAM];
    uint64_t output_length;
    char errors[BUSTER_CHECK_LANES_EXIT_STREAM];
    uint64_t errors_length;
    char threads[32];
    uint32_t helpers;
    uint32_t helpers_ended;
    bool helpers_timed_out;
    bool streams_ended_first;
};

// Reads what `fd` (nonblocking) has into `buffer` past *length, up to its room: false once the stream has ended.
static bool check_lanes_test_exit_read(int fd, char* buffer, uint64_t* length)
{
    char scratch[4096];
    const uint64_t room = BUSTER_CHECK_LANES_EXIT_STREAM - *length;
    const ssize_t got = read(fd, room != 0 ? buffer + *length : scratch, room != 0 && room < sizeof(scratch) ? room : sizeof(scratch));
    *length += got > 0 && room != 0 ? (uint64_t)got : 0;
    return got != 0;
}

// Runs `arguments` (the driver's command line, its executable first) with this process's environment but for
// BUSTER_EXIT_HELPER, BUSTER_TEST_EXIT_HELPER_PAUSE and BUSTER_TEST_THREADS, and `helper` and `pause` set when nonnull,
// the thread count written to `threads_path`; its streams read until both end, then the driver waited for, then, this
// process a subreaper for the run, every process that became its child meanwhile, up to BUSTER_CHECK_LANES_EXIT_SECONDS.
static void check_lanes_test_exit_run(CheckLanesExitRun* run, char* const* arguments, const char* helper, const char* pause, const char* threads_path)
{
    run->status = -1;
    run->output_length = 0;
    run->errors_length = 0;
    run->threads[0] = 0;
    run->helpers = 0;
    run->helpers_ended = 0;
    run->helpers_timed_out = false;
    run->streams_ended_first = false;
    uint64_t inherited = 0;
    for (char** entry = environ; entry != NULL && *entry != NULL; entry += 1)
    {
        inherited += 1;
    }
    char helper_entry[64];
    char pause_entry[64];
    char threads_entry[PATH_MAX + 32];
    snprintf(helper_entry, sizeof(helper_entry), "BUSTER_EXIT_HELPER=%s", helper != NULL ? helper : "");
    snprintf(pause_entry, sizeof(pause_entry), "BUSTER_TEST_EXIT_HELPER_PAUSE=%s", pause != NULL ? pause : "");
    snprintf(threads_entry, sizeof(threads_entry), "BUSTER_TEST_THREADS=%s", threads_path);
    char** environment = calloc(inherited + 4, sizeof(char*));
    uint64_t used = 0;
    for (uint64_t i = 0; environment != NULL && i < inherited; i += 1)
    {
        if (strncmp(environ[i], "BUSTER_EXIT_HELPER=", 19) != 0 && strncmp(environ[i], "BUSTER_TEST_EXIT_HELPER_PAUSE=", 30) != 0 &&
            strncmp(environ[i], "BUSTER_TEST_THREADS=", 20) != 0)
        {
            environment[used] = environ[i];
            used += 1;
        }
    }
    if (environment != NULL)
    {
        environment[used] = threads_entry;
        used += 1;
        environment[used] = helper != NULL ? helper_entry : NULL;
        used += helper != NULL ? 1 : 0;
        environment[used] = pause != NULL ? pause_entry : NULL;
    }
    unlink(threads_path);
    int output_pipe[2] = { -1, -1 };
    int error_pipe[2] = { -1, -1 };
    bool piped = environment != NULL && pipe(output_pipe) == 0 && pipe(error_pipe) == 0;
    // Close-on-exec, so that the driver has only the write ends its standard streams are made of.
    const int all[4] = { output_pipe[0], output_pipe[1], error_pipe[0], error_pipe[1] };
    for (uint32_t i = 0; piped && i < 4; i += 1)
    {
        piped = fcntl(all[i], F_SETFD, FD_CLOEXEC) == 0;
    }
    const bool reaping = piped && prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0;
    posix_spawn_file_actions_t actions;
    const bool acted = reaping && posix_spawn_file_actions_init(&actions) == 0;
    pid_t child = -1;
    const bool spawned = acted && posix_spawn_file_actions_adddup2(&actions, output_pipe[1], 1) == 0 &&
                         posix_spawn_file_actions_adddup2(&actions, error_pipe[1], 2) == 0 &&
                         posix_spawn(&child, arguments[0], &actions, 0, arguments, environment) == 0;
    if (acted)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    // The write ends are the driver's alone.
    const int ends[2] = { output_pipe[1], error_pipe[1] };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (ends[i] >= 0)
        {
            close(ends[i]);
        }
    }
    const struct timespec pause_step = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };
    if (spawned)
    {
        fcntl(output_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(error_pipe[0], F_SETFL, O_NONBLOCK);
        bool output_open = true;
        bool errors_open = true;
        pid_t waited = 0;
        int status = 0;
        // Both streams to their ends, for at most a minute; then, as they end, whether the driver still runs.
        for (uint64_t milliseconds = 0; (output_open || errors_open) && milliseconds < 60000; milliseconds += 1)
        {
            output_open = output_open && check_lanes_test_exit_read(output_pipe[0], run->output, &run->output_length);
            errors_open = errors_open && check_lanes_test_exit_read(error_pipe[0], run->errors, &run->errors_length);
            if (output_open || errors_open)
            {
                nanosleep(&pause_step, 0);
            }
        }
        waited = waitpid(child, &status, WNOHANG);
        run->streams_ended_first = !output_open && !errors_open && waited == 0;
        if (waited == 0)
        {
            waited = waitpid(child, &status, 0);
        }
        run->status = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        // The driver's helper, if it started one, is this process's child now: the driver's exit reparented it before
        // its parent learned of the exit.
        bool any = true;
        for (uint64_t milliseconds = 0; any && milliseconds < 1000ull * BUSTER_CHECK_LANES_EXIT_SECONDS; milliseconds += 1)
        {
            siginfo_t info;
            memset(&info, 0, sizeof(info));
            const int found = waitid(P_ALL, 0, &info, WEXITED | WNOHANG);
            any = found == 0;
            if (found == 0 && info.si_pid != 0)
            {
                run->helpers += 1;
                run->helpers_ended += (info.si_code == CLD_KILLED && info.si_status == SIGKILL) || (info.si_code == CLD_EXITED && info.si_status == 0) ? 1 : 0;
            }
            else if (found == 0)
            {
                nanosleep(&pause_step, 0);
            }
        }
        run->helpers_timed_out = any;
    }
    if (reaping)
    {
        prctl(PR_SET_CHILD_SUBREAPER, 0, 0, 0, 0);
    }
    const int reads[2] = { output_pipe[0], error_pipe[0] };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (reads[i] >= 0)
        {
            close(reads[i]);
        }
    }
    FILE* file = fopen(threads_path, "r");
    if (file != NULL && fgets(run->threads, sizeof(run->threads), file) == NULL)
    {
        run->threads[0] = 0;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    free(environment);
}

// The exit helper (#1444, lane_exit_helper_start), on `directory`'s generated programs: each command by
// BUSTER_EXIT_HELPER=0, =1 and by default, with the same status, standard output and error and object each way; one
// helper by =1 where the driver's lanes created no thread (none in a sanitizer build), and by default where this
// machine's PID 1 reaps orphans too (lane_exit_reaped), none otherwise and none by =0, each helper ended (by its SIGKILL,
// or at once) within BUSTER_CHECK_LANES_EXIT_SECONDS of the driver; and a small object by =1 with the driver held after
// its helper's start (BUSTER_TEST_EXIT_HELPER_PAUSE), whose streams must end while it still runs: the helper holds none
// of its descriptors.
static void check_lanes_test_exit_helper(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs)
{
    char small[PATH_MAX];
    char large[PATH_MAX];
    char warned[PATH_MAX];
    char failing[PATH_MAX];
    char threads_path[PATH_MAX];
    snprintf(small, sizeof(small), "%s/program_2.c", directory);
    snprintf(large, sizeof(large), "%s/program_%u.c", directory, programs);
    snprintf(warned, sizeof(warned), "%s/exit_helper_warned.c", directory);
    snprintf(failing, sizeof(failing), "%s/exit_helper_failing.c", directory);
    snprintf(threads_path, sizeof(threads_path), "%s/exit_helper.threads", directory);
    static const char warned_text[] = "#warning the exit helper's stream\nint f(void) { return 1; }\n";
    static const char failing_text[] = "int f(void) { return undeclared; }\n";
    const bool written = parse_test_write(warned, warned_text, sizeof(warned_text) - 1) && parse_test_write(failing, failing_text, sizeof(failing_text) - 1);
    check_lanes_test_expect(tally, written, "write the exit helper's sources", directory);
    typedef struct CheckLanesExitCommand CheckLanesExitCommand;
    struct CheckLanesExitCommand
    {
        const char* options[3];
        const char* source;
        bool object;
    };
    const CheckLanesExitCommand commands[] = {
        { .options = { "-c" }, .source = small, .object = true },
        { .options = { "-E" }, .source = small },
        { .options = { "-fsyntax-only" }, .source = warned },
        { .options = { "-c" }, .source = failing, .object = true },
        { .options = { "-c" }, .source = large, .object = true },
    };
    // BUSTER_EXIT_HELPER=0, =1 and unset.
    static const char* const ways[3] = { "0", "1", NULL };
    static CheckLanesExitRun runs[3];
    const bool reaped = lane_exit_reaped();
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(commands); c += 1)
    {
        const CheckLanesExitCommand* command = commands + c;
        char objects[3][PATH_MAX];
        for (uint32_t way = 0; way < 3; way += 1)
        {
            snprintf(objects[way], sizeof(objects[way]), "%s/exit_helper_%u_%u.o", directory, c, way);
            unlink(objects[way]);
            char* arguments[8] = { (char*)executable };
            uint32_t count = 1;
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(command->options) && command->options[o] != NULL; o += 1)
            {
                arguments[count++] = (char*)command->options[o];
            }
            if (command->object)
            {
                arguments[count++] = (char*)"-o";
                arguments[count++] = objects[way];
            }
            arguments[count++] = (char*)command->source;
            arguments[count] = NULL;
            check_lanes_test_exit_run(&runs[way], arguments, ways[way], NULL, threads_path);
        }
        // A helper by =1 where the driver's lanes created no thread, but in a sanitizer build, and by default where
        // orphans are reaped too.
        const uint32_t forced = !BUSTER_SANITIZER_RESOURCES && strcmp(runs[1].threads, "0\n") == 0 ? 1 : 0;
        const uint32_t expected[3] = { 0, forced, reaped ? forced : 0 };
        for (uint32_t way = 0; way < 3; way += 1)
        {
            const CheckLanesExitRun* off = &runs[0];
            const CheckLanesExitRun* run = &runs[way];
            char detail[PATH_MAX + 256];
            snprintf(detail, sizeof(detail), "command %u (%s %s), BUSTER_EXIT_HELPER %s: status %d (%d without), %llu output bytes (%llu), %llu error bytes (%llu), lane threads %.8s, %u helpers (%u expected), %u ended%s",
                     c, command->options[0], command->source, ways[way] != NULL ? ways[way] : "unset", run->status, off->status,
                     (unsigned long long)run->output_length, (unsigned long long)off->output_length, (unsigned long long)run->errors_length,
                     (unsigned long long)off->errors_length, run->threads, run->helpers, expected[way], run->helpers_ended, run->helpers_timed_out ? ", timed out" : "");
            const bool same_streams = run->status == off->status && off->status >= 0 && run->output_length == off->output_length &&
                                      run->errors_length == off->errors_length && memcmp(run->output, off->output, off->output_length) == 0 &&
                                      memcmp(run->errors, off->errors, off->errors_length) == 0;
            check_lanes_test_expect(tally, same_streams, "the driver's status, output and errors are the same with its exit helper and without", detail);
            check_lanes_test_expect(tally, !command->object || run->status != 0 || check_test_driver_same_files(objects[0], objects[way]),
                                    "the driver's object is the same with its exit helper and without", detail);
            check_lanes_test_expect(tally, run->threads[0] != 0 && run->helpers == expected[way] && run->helpers_ended == expected[way] && !run->helpers_timed_out,
                                    "the driver starts an exit helper as BUSTER_EXIT_HELPER, its lanes and PID 1 say, which ends with it", detail);
        }
    }
    // Held after its helper's start, the driver's streams end while it still runs: neither it nor its helper holds them.
    // Without a helper (a sanitizer build) the order is the kernel's, not the driver's: a threaded process's files can
    // close while another of its threads still tears its address space down, long before its exit can be waited for, and
    // the merge-queue drill (#1430) saw TSan drivers end both ways. So the order is checked only where a helper sets it.
    char* held[] = { (char*)executable, (char*)"-c", (char*)"-o", (char*)"/dev/null", small, NULL };
    check_lanes_test_exit_run(&runs[0], held, "1", "2000", threads_path);
    const bool helped = !BUSTER_SANITIZER_RESOURCES;
    char detail[256];
    snprintf(detail, sizeof(detail), "status %d, streams ended first: %d, %u helpers, %u ended%s", runs[0].status, runs[0].streams_ended_first, runs[0].helpers,
             runs[0].helpers_ended, runs[0].helpers_timed_out ? ", timed out" : "");
    check_lanes_test_expect(tally, runs[0].status == 0 && (!helped || runs[0].streams_ended_first) && runs[0].helpers == (helped ? 1u : 0u) &&
                                       runs[0].helpers_ended == runs[0].helpers && !runs[0].helpers_timed_out,
                            "a reader of the driver's streams sees them end without waiting for its exit helper", detail);
    printf("  exit helper: %u commands by BUSTER_EXIT_HELPER=0, =1 and by default (orphans %s here), and a driver held after the helper's start\n",
           (uint32_t)BUSTER_ARRAY_LENGTH(commands), reaped ? "reaped" : "not reaped");
}

// The commands of check_lanes_test_session_commands: their arguments (but the executable's), and what each must return.
#define BUSTER_CHECK_LANES_SESSION_COMMANDS 6
#define BUSTER_CHECK_LANES_SESSION_ROUNDS 3
#define BUSTER_CHECK_LANES_SESSION_LANES 4
typedef struct CheckLanesSession CheckLanesSession;
struct CheckLanesSession
{
    char* arguments[BUSTER_CHECK_LANES_SESSION_COMMANDS][12];
    uint32_t counts[BUSTER_CHECK_LANES_SESSION_COMMANDS];
    int expected[BUSTER_CHECK_LANES_SESSION_COMMANDS];
    // The object each command writes, or NULL, and --lanes 1's.
    const char* objects[BUSTER_CHECK_LANES_SESSION_COMMANDS];
    const char* reference;
    pid_t threads[BUSTER_CHECK_LANES_SESSION_ROUNDS][BUSTER_CHECK_LANES_SESSION_COMMANDS][BUSTER_CHECK_LANES_SESSION_LANES];
    uint64_t failures;
    uint64_t created;
    uint32_t lanes;
};

// Lane 0's checks of a command's result, after it: its status, its object, and no thread created since the session
// started.
static void check_lanes_test_session_result(CheckLanesSession* session, uint32_t c, int status)
{
    session->failures += status != session->expected[c] ? 1 : 0;
    session->failures += session->objects[c] != NULL && !check_test_driver_same_files(session->objects[c], session->reference) ? 1 : 0;
    session->failures += lane_process.threads_created != session->created ? 1 : 0;
}

// The commands' rounds, every lane calling each command (a collective call): each lane the same thread throughout, and
// the session's barrier advancing.
static void check_lanes_test_session_flow(void* argument)
{
    CheckLanesSession* session = argument;
    const uint32_t index = lane_index();
    if (index == 0)
    {
        session->lanes = lane_count();
    }
    uint32_t generation = 0;
    for (uint32_t round = 0; round < BUSTER_CHECK_LANES_SESSION_ROUNDS; round += 1)
    {
        for (uint32_t c = 0; c < BUSTER_CHECK_LANES_SESSION_COMMANDS; c += 1)
        {
            session->threads[round][c][index] = (pid_t)syscall(SYS_gettid);
            const int status = run_check_command((int)session->counts[c], session->arguments[c], true);
            lane_park();
            if (index == 0)
            {
                const uint32_t now = __atomic_load_n(&thread_context.session->group.generation, __ATOMIC_ACQUIRE) & ~1u;
                session->failures += round + c != 0 && now == generation ? 1 : 0;
                generation = now;
                check_lanes_test_session_result(session, c, status);
            }
        }
    }
}

// Whether any lane of the commands' rounds ran on another thread than its first, or on the thread of the lane before it.
static uint64_t check_lanes_test_session_moved(const CheckLanesSession* session)
{
    uint64_t moved = 0;
    for (uint32_t round = 0; round < BUSTER_CHECK_LANES_SESSION_ROUNDS; round += 1)
    {
        for (uint32_t c = 0; c < BUSTER_CHECK_LANES_SESSION_COMMANDS; c += 1)
        {
            for (uint32_t lane = 0; lane < BUSTER_CHECK_LANES_SESSION_LANES; lane += 1)
            {
                moved += session->threads[round][c][lane] != session->threads[0][0][lane] ? 1 : 0;
                moved += lane != 0 && session->threads[round][c][lane] == session->threads[round][c][lane - 1] ? 1 : 0;
            }
        }
    }
    return moved;
}

// Commands in one process (see the file's comment).
static void check_lanes_test_session_commands(CheckLanesTestTally* tally, const char* executable, const char* directory)
{
    static CheckLanesSession session;
    memset(&session, 0, sizeof(session));
    char program[PATH_MAX];
    char second[PATH_MAX];
    char broken[PATH_MAX];
    char reference[PATH_MAX];
    char four[PATH_MAX];
    char two[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(program, sizeof(program), "%s/program_3.c", directory);
    snprintf(second, sizeof(second), "%s/program_2.c", directory);
    snprintf(broken, sizeof(broken), "%s/session_broken.c", directory);
    snprintf(reference, sizeof(reference), "%s/session_reference.o", directory);
    snprintf(four, sizeof(four), "%s/session_four.o", directory);
    snprintf(two, sizeof(two), "%s/session_two.o", directory);
    snprintf(errors, sizeof(errors), "%s/session.errors", directory);
    FILE* file = fopen(broken, "w");
    const bool written = file != NULL && fputs("int broken(void) { return ; }\nint missing(void) { return }\n", file) >= 0;
    const bool closed = file != NULL && fclose(file) == 0;
    char* reference_arguments[] = { (char*)executable, (char*)"--lanes", (char*)"1", (char*)"-c", (char*)"-o", reference, program, NULL };
    const bool referenced = written && closed && check_lanes_test_run(reference_arguments, errors) == 0;
    check_lanes_test_expect(tally, referenced, "the session's reference object and failing source", program);
    // Two sources' commands between the others: each runs its flow's rounds on the session's lanes, after which the next
    // ones run as before.
    char* commands[BUSTER_CHECK_LANES_SESSION_COMMANDS][8] = {
        { (char*)"-c", (char*)"-o", (char*)"/dev/null", broken, NULL },
        { (char*)"--lanes", (char*)"3", (char*)"-fsyntax-only", second, program, NULL },
        { (char*)"--lanes", (char*)"4", (char*)"-c", (char*)"-o", four, program, NULL },
        { (char*)"--lanes", (char*)"1", (char*)"-fsyntax-only", second, program, NULL },
        { (char*)"--lanes", (char*)"4", (char*)"--worker-budget", (char*)"2", (char*)"-c", (char*)"-o", two, program },
        { (char*)"-fsyntax-only", program, NULL },
    };
    const int expected[BUSTER_CHECK_LANES_SESSION_COMMANDS] = { 1, 0, 0, 0, 0, 0 };
    const char* objects[BUSTER_CHECK_LANES_SESSION_COMMANDS] = { NULL, NULL, four, NULL, two, NULL };
    for (uint32_t c = 0; c < BUSTER_CHECK_LANES_SESSION_COMMANDS; c += 1)
    {
        uint32_t count = 0;
        while (count < 8 && commands[c][count] != NULL)
        {
            session.arguments[c][count] = commands[c][count];
            count += 1;
        }
        session.counts[c] = count;
        session.expected[c] = expected[c];
        session.objects[c] = objects[c];
    }
    session.reference = reference;
    // The failing command's diagnostics would go to the test's standard error.
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    const bool silenced = saved >= 0 && quiet >= 0 && dup2(quiet, STDERR_FILENO) == STDERR_FILENO;
    static LaneSession lanes;
    const uint64_t before = lane_process.threads_created;
    session.created = before + BUSTER_CHECK_LANES_SESSION_LANES - 1;
    const uint32_t ran = referenced && silenced ? lane_session_run(&lanes, BUSTER_CHECK_LANES_SESSION_LANES, NULL, check_lanes_test_session_flow, &session, 0) : 0;
    const uint64_t flow_failures = session.failures;
    const uint64_t moved = check_lanes_test_session_moved(&session);
    const uint32_t eager_lanes = session.lanes;
    // The same on a lazy session, whose lane 1 creates the others while lane 0 runs the first command.
    session.failures = 0;
    session.lanes = 0;
    session.created = before + 2 * (BUSTER_CHECK_LANES_SESSION_LANES - 1);
    const uint32_t lazy = ran != 0 ? lane_session_run(&lanes, BUSTER_CHECK_LANES_SESSION_LANES, NULL, check_lanes_test_session_flow, &session, BUSTER_LANE_SESSION_LAZY) : 0;
    const uint64_t lazy_moved = check_lanes_test_session_moved(&session);
    fflush(stderr);
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (quiet >= 0)
    {
        close(quiet);
    }
    char detail[160];
    snprintf(detail, sizeof(detail), "%u and %u lanes ran, %llu and %llu failures, %llu and %llu thread changes", ran, lazy, (unsigned long long)flow_failures,
             (unsigned long long)session.failures, (unsigned long long)moved, (unsigned long long)lazy_moved);
    check_lanes_test_expect(tally, ran == BUSTER_CHECK_LANES_SESSION_LANES && eager_lanes == BUSTER_CHECK_LANES_SESSION_LANES && flow_failures == 0 && moved == 0,
                            "commands in one process run on one session's lanes, collectively", detail);
    check_lanes_test_expect(tally, lazy == BUSTER_CHECK_LANES_SESSION_LANES && session.lanes == BUSTER_CHECK_LANES_SESSION_LANES && session.failures == 0 &&
                                       lazy_moved == 0 && lane_process.threads_created == before + 2 * (BUSTER_CHECK_LANES_SESSION_LANES - 1),
                            "commands in one process run on a lazy session's lanes, collectively", detail);
}

// A growing team in a command in this process (#673's tail; see the file's comment): its command and exit status.
typedef struct CheckLanesGrowthSession CheckLanesGrowthSession;
struct CheckLanesGrowthSession
{
    char* arguments[12];
    uint32_t count;
    int status;
};

static void check_lanes_test_growth_flow(void* argument)
{
    CheckLanesGrowthSession* growth = argument;
    const int status = run_check_command((int)growth->count, growth->arguments, true);
    lane_park();
    if (lane_index() == 0)
    {
        growth->status = status;
    }
}

// The first program of 600 functions, `program_N.c` for N `programs`, by --lanes 4 in a command on a session of 4 lanes
// in this process, with `cap` workers by --worker-budget (0 for none), under a jobserver pipe without a token, whose lane
// 0 supplies 3 after its first item, and if `slow`, whose lane 1 sleeps 20 ms before each part's lowering it takes
// (BUSTER_TEST_SLOW_LANE): the team must grow to 4 lanes, or to the cap, which the session's lanes don't bound here as a
// command's own session's do; the pipe must hold the 3 as the command returns, while this process and anything it leaked
// still live; and the object must be the growth's reference (check_lanes_test_driver_growth's, --lanes 1's), the slow
// lane's parts' code taken by lanes that waited for their lowering.
static void check_lanes_test_session_growth(CheckLanesTestTally* tally, const char* directory, uint32_t programs, uint32_t cap, bool slow)
{
    static const uint8_t supplied[] = { BUSTER_CHECK_EMIT_SUPPLY_BYTE, BUSTER_CHECK_EMIT_SUPPLY_BYTE + 1, BUSTER_CHECK_EMIT_SUPPLY_BYTE + 2 };
    char program[PATH_MAX];
    char reference[PATH_MAX];
    char object[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(program, sizeof(program), "%s/program_%u.c", directory, programs);
    snprintf(reference, sizeof(reference), "%s/growth_reference_0.o", directory);
    snprintf(object, sizeof(object), "%s/session_growth.o", directory);
    snprintf(errors, sizeof(errors), "%s/session_growth.errors", directory);
    int fds[2] = { -1, -1 };
    // The caller's MAKEFLAGS and test hook, as a make that runs the test sets them, restored after.
    const char* caller_makeflags = getenv("MAKEFLAGS");
    const char* caller_supply = getenv("BUSTER_TEST_WORKER_SUPPLY");
    const char* caller_slow = getenv("BUSTER_TEST_SLOW_LANE");
    char* saved_makeflags = caller_makeflags != NULL ? strdup(caller_makeflags) : NULL;
    char* saved_supply = caller_supply != NULL ? strdup(caller_supply) : NULL;
    char* saved_slow = caller_slow != NULL ? strdup(caller_slow) : NULL;
    const bool saved = (caller_makeflags == NULL || saved_makeflags != NULL) && (caller_supply == NULL || saved_supply != NULL) &&
                       (caller_slow == NULL || saved_slow != NULL);
    const bool opened = saved && check_driver_cpus() >= 4 && pipe(fds) == 0;
    char makeflags[64];
    snprintf(makeflags, sizeof(makeflags), "-j --jobserver-auth=%d,%d", fds[0], fds[1]);
    const bool set = opened && setenv("MAKEFLAGS", makeflags, 1) == 0 && setenv("BUSTER_TEST_WORKER_SUPPLY", "3", 1) == 0 &&
                     (slow ? setenv("BUSTER_TEST_SLOW_LANE", "2,20000", 1) : unsetenv("BUSTER_TEST_SLOW_LANE")) == 0;
    static CheckLanesGrowthSession growth;
    char workers[32];
    snprintf(workers, sizeof(workers), "--worker-budget=%u", cap);
    char* const arguments[] = { (char*)"--lanes", (char*)"4", (char*)"-c", (char*)"--time-stages", (char*)"-o", object, program, workers };
    growth = (CheckLanesGrowthSession){ .count = cap != 0 ? 8 : 7, .status = -1 };
    for (uint32_t i = 0; i < growth.count; i += 1)
    {
        growth.arguments[i] = arguments[i];
    }
    const uint32_t active = cap != 0 ? cap : 4;
    fflush(stderr);
    const int saved_errors = dup(STDERR_FILENO);
    const int report = open(errors, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool redirected = set && saved_errors >= 0 && report >= 0 && dup2(report, STDERR_FILENO) == STDERR_FILENO;
    static LaneSession lanes;
    const uint32_t ran = redirected ? lane_session_run(&lanes, 4, NULL, check_lanes_test_growth_flow, &growth, 0) : 0;
    fflush(stderr);
    if (saved_errors >= 0)
    {
        dup2(saved_errors, STDERR_FILENO);
        close(saved_errors);
    }
    if (report >= 0)
    {
        close(report);
    }
    const bool restored = saved && (saved_makeflags != NULL ? setenv("MAKEFLAGS", saved_makeflags, 1) == 0 : unsetenv("MAKEFLAGS") == 0) &&
                          (saved_supply != NULL ? setenv("BUSTER_TEST_WORKER_SUPPLY", saved_supply, 1) == 0 : unsetenv("BUSTER_TEST_WORKER_SUPPLY") == 0) &&
                          (saved_slow != NULL ? setenv("BUSTER_TEST_SLOW_LANE", saved_slow, 1) == 0 : unsetenv("BUSTER_TEST_SLOW_LANE") == 0);
    check_lanes_test_expect(tally, restored, "the growing team's command restores the caller's MAKEFLAGS and test hooks", NULL);
    free(saved_makeflags);
    free(saved_supply);
    free(saved_slow);
    if (opened)
    {
        uint32_t reported[6] = { 0 };
        const bool found = check_lanes_test_report_budget(errors, reported);
        char detail[160];
        snprintf(detail, sizeof(detail), "%u lanes ran, status %d, granted %u, active %u, grown %u, worked %u", ran, growth.status, reported[2], reported[3],
                 reported[4], reported[5]);
        check_lanes_test_expect(tally, ran == 4 && growth.status == 0 && check_test_driver_same_files(object, reference),
                                slow ? "a growing team whose lanes wait for a slow lane's lowering writes --lanes 1's object" :
                                       "a growing team in a command in this process writes --lanes 1's object",
                                detail);
        check_lanes_test_expect(tally, found && reported[2] == 1 && reported[3] == active && reported[4] == active - 1 && reported[5] <= active,
                                cap != 0 ? "a growing team in a command in this process grows no further than --worker-budget" :
                                           "a growing team in a command in this process grows by the tokens supplied",
                                detail);
        check_lanes_test_expect(tally, worker_budget_test_tokens(fds[0], supplied, sizeof(supplied)),
                                "a growing team returns every token before its command returns", detail);
        close(fds[0]);
        close(fds[1]);
    }
}

static void check_tier1_outcome_tests(CheckLanesTestTally* tally, const char* directory, const char* executable);

static int run_check_lanes_tests(int argc, char** argv)
{
    CheckLanesTestTally tally = { .random = 0x9e3779b97f4a7c15ull };
    const char* executable = "/proc/self/exe";
    bool keep = false;
    uint32_t programs = 6;
    bool tier1_outcomes_only = false;
    uint64_t id_order = 153;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            tally.random ^= strtoull(argv[++i], 0, 0) * 0x2545f4914f6cdd1dull;
            tally.random |= 1;
        }
        else if (strcmp(argv[i], "--programs") == 0 && i + 1 < argc)
        {
            programs = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--id-order") == 0 && i + 1 < argc)
        {
            char* end = NULL;
            id_order = strtoull(argv[++i], &end, 10);
            options = end != argv[i] && *end == 0;
        }
        else if (strcmp(argv[i], "--tier1-outcomes") == 0)
        {
            tier1_outcomes_only = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            options = false;
        }
    }
    check_lanes_test_expect(&tally, options, "options: [--executable PATH] [--seed N] [--programs N] [--id-order N] [--tier1-outcomes] [--keep]", 0);
    char resolved[PATH_MAX];
    const ssize_t resolved_length = strcmp(executable, "/proc/self/exe") == 0 ? readlink(executable, resolved, sizeof(resolved) - 1) : -1;
    if (resolved_length > 0)
    {
        resolved[resolved_length] = 0;
        executable = resolved;
    }
    char directory[PATH_MAX];
    const char* temporary = oracle_temporary_directory();
    snprintf(directory, sizeof(directory), "%s/buster-check-lanes-XXXXXX", temporary);
    const bool made = options && mkdtemp(directory) != 0;
    check_lanes_test_expect(&tally, made, "make the test's directory", directory);
    if (made)
    {
        check_tier1_outcome_tests(&tally, directory, executable);
        if (!tier1_outcomes_only)
        {
            check_lanes_test_backend_startup(&tally, directory);
            check_lanes_test_expect(&tally, worker_budget_test(), "external worker ownership protocol", NULL);
            const bool initialized = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
            check_lanes_test_expect(&tally, initialized, "initialize the backend", 0);
            check_lanes_test_expect(&tally, check_lanes_test_order_setup(), "refuse an id-order priming setup that fails", NULL);
            check_lanes_test_expect(&tally, check_lanes_test_order_cache(directory), "prime from a private cache while retaining source-error names", NULL);
            check_lanes_test_programs(&tally, directory, programs, id_order);
            check_lanes_test_failing(&tally, directory, id_order);
            check_lanes_test_squeezes(&tally, directory);
            check_lanes_test_unstarted(&tally, directory);
            check_lanes_test_address_space(&tally, directory);
            check_lanes_test_scratch(&tally, directory);
            check_lanes_test_settle(&tally, directory);
            check_lanes_test_rule(&tally, directory);
            check_lanes_test_driver(&tally, executable, directory, programs, id_order);
            check_lanes_test_driver_failing(&tally, executable, directory, id_order);
            check_lanes_test_driver_deferred(&tally, executable, directory, programs, id_order);
            check_lanes_test_grow_gate(&tally);
            check_lanes_test_driver_growth(&tally, executable, directory, programs);
            check_lanes_test_session_threads(&tally, executable, directory, programs);
            check_lanes_test_exit_helper(&tally, executable, directory, programs);
            check_lanes_test_session_commands(&tally, executable, directory);
            check_lanes_test_session_growth(&tally, directory, programs, 0, false);
            check_lanes_test_session_growth(&tally, directory, programs, 2, false);
            check_lanes_test_session_growth(&tally, directory, programs, 0, true);
            check_lanes_test_benchmark_sections(&tally, executable, directory);
        }
        if (!keep && tally.failures == 0)
        {
            char* remove[] = { (char*)"rm", (char*)"-rf", directory, 0 };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("  kept %s\n", directory);
        }
    }
    printf("check lanes: %llu checks, %llu failures\n", (unsigned long long)tally.checks, (unsigned long long)tally.failures);
    return tally.failures == 0 ? 0 : 1;
}
