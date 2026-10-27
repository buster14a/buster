// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-check-lanes [--executable PATH] [--seed N] [--programs N] [--keep]: the functions' lanes (#151, and #153's item
// for them), check_program_emit_lanes. Every output must be one lane's, whatever the lanes and whichever finishes first.
//
// - Programs: generated programs (--programs, 6 by default, and two of 600 functions) of functions of many kinds, among
//   them 128-bit integers' division and conversions and complex products (libgcc's functions, which the lowering numbers
//   in the order the bodies first call them), computed gotos through static tables of labels' addresses (the label
//   symbols), switches, structs by value, long doubles, variadic calls and statics, in a random order, each compiled
//   through the pipeline once by each path, then emitted on one lane (check_program_emit) and on 2, 3 and 8 lanes with
//   every lane forced (CheckProgramLanes.every_lane), and on 8 with lanes 0, 1 and 7 each slow in turn (so that the
//   others take its parts and finish first), with -g's rows and unwind tables and without: each the same object
//   (object_build of the emission's link and the static data), byte for byte, as one lane's, with the lanes and parts
//   the emission says it ran.
// - The lane rule: a program's lanes by default (CheckProgramLanes.lanes 8, not every lane), one for few tokens, and
//   more with more bodies than twice the largest's, never more than the bodies.
// - Failures: programs whose bodies fail at different steps, in different orders (a lowering's refusal by name, Tier 0's
//   refusal, the calling convention's), emitted as above: the same status, token, entry, reason and Tier 0 error at every
//   lane count as one lane's, which are source order's (the steps' order, then the bodies').
// - The driver (with --executable, this executable by default): four of the generated programs' objects (-c) and
//   executables (one of 3 functions, two of random sizes and the first of 600) by --lanes 1, 3 and 8 and by default, by
//   each path: the same bytes, and the executables exit with the same status.

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
                                "    long r = 0;\n    switch (a & 7)\n    {\n"
                                "        case 0: r = b; break;\n        case 1: r = b * 2; break;\n        case 2: r = b - a; break;\n"
                                "        case 3: r = a * a; break;\n        case 5: r = b ^ 5; break;\n        default: r = -b; break;\n    }\n");
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
// which the caller keeps and releases.
static void check_lanes_test_compile(CheckLanesTestUnit* unit, Preprocessor* pp, Parser* parser, InternTable* identifiers, const char* path, bool scalar)
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
    ready = ready && intern_initialize(identifiers) && pp_initialize(pp, &options, identifiers) && parse_initialize(parser, &parse_options, options.language) &&
            pp_run(pp, input) && pp->errors == 0 && parse_translation_unit(parser, pp) && parser->errors == 0;
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
            check_data_prepare(&unit->data, &unit->input, &unit->stream, &unit->initializers, &unit->types, &unit->output, &unit->scratch);
    const CheckProgramOptions program_options = {
        .initializers = &unit->initializers,
        .asms = &unit->asms,
        .identifiers = identifiers,
        .initializer_references = unit->data.references,
        .initializer_reference_count = unit->data.reference_count,
        .scalar = scalar,
    };
    ready = ready && check_program(&unit->program, parser, &unit->input, &unit->stream, &unit->types, program_options, &unit->output, &unit->scratch) &&
            check_program_select(&unit->selected, &unit->program, &unit->data, scalar, &unit->output, &unit->scratch);
    unit->ready = ready;
}

// One emission of a compiled unit and what it made: its object's bytes (malloc'd, the caller frees them), or its
// failure, and the lanes and parts it ran.
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
    const char* reason;
    uint32_t active;
    uint32_t parts;
    bool again;
};

// Emits `unit`'s selected program on `lanes` (NULL for check_program_emit's one), with -g's rows (`rows`) and unwind
// tables (`unwind`), then publishes its static data and builds its object, as check_driver_object does.
static CheckLanesTestEmission check_lanes_test_emit(CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp, const InternTable* identifiers,
                                                    CheckProgramLanes* lanes, bool scalar, bool rows, bool unwind)
{
    CheckLanesTestEmission result = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 32);
    Arena bytes = arena_reserve(UINT64_C(1) << 32);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    CheckProgramCode code = { 0 };
    CheckProgramRows row_set = { 0 };
    CheckDebug dwarf = { 0 };
    const CheckProgram* program = &unit->selected.program;
    const bool emitted = output.base != 0 && bytes.base != 0 && scratch.base != 0 &&
                         (lanes != NULL ? check_program_emit_lanes(&code, program, parser, identifiers, unit->selected.data.capacity, scalar, &output, &bytes, &scratch,
                                                                   NULL, rows ? &row_set : NULL, unwind, true, lanes) :
                                          check_program_emit(&code, program, parser, identifiers, unit->selected.data.capacity, scalar, &output, &bytes, &scratch, NULL,
                                                             rows ? &row_set : NULL, unwind, true));
    result.emitted = emitted;
    result.status = code.status;
    result.token = code.token;
    result.backend_node = code.backend_node;
    result.backend_error = code.backend_error;
    result.reason = code.reason;
    result.active = lanes != NULL ? lanes->active : 1;
    result.parts = lanes != NULL ? lanes->parts : 1;
    result.again = lanes != NULL && lanes->again;
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

// Whether two emissions made the same: the same object, or the same failure.
static bool check_lanes_test_same(const CheckLanesTestEmission* a, const CheckLanesTestEmission* b)
{
    bool same = a->emitted == b->emitted && a->built == b->built && a->status == b->status && a->token == b->token && a->backend_node == b->backend_node &&
                a->backend_error == b->backend_error && ((a->reason == NULL) == (b->reason == NULL)) &&
                (a->reason == NULL || strcmp(a->reason, b->reason) == 0) && a->size == b->size;
    return same && (a->size == 0 || memcmp(a->object, b->object, a->size) == 0);
}

// `unit` emitted on one lane, then on 2, 3 and 8 lanes, every lane forced, and on 8 with lanes 0, 1 and 7 each slow in
// turn, with and without -g's rows and unwind tables: each emission the one lane's. Returns how many emissions it
// compared, and `emitted` whether the one lane's succeeded.
static uint32_t check_lanes_test_unit(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp,
                                      const InternTable* identifiers, bool scalar, const char* what, bool* emitted)
{
    static const uint32_t counts[] = { 2, 3, 8 };
    static const uint32_t slow[] = { 1, 2, 8 };
    uint32_t compared = 0;
    char detail[256];
    *emitted = false;
    for (uint32_t shape = 0; shape < 2; shape += 1)
    {
        const bool rows = shape == 1;
        const bool unwind = shape == 1;
        CheckLanesTestEmission one = check_lanes_test_emit(unit, parser, pp, identifiers, NULL, scalar, rows, unwind);
        *emitted |= one.emitted;
        for (uint32_t run = 0; run < BUSTER_ARRAY_LENGTH(counts) + BUSTER_ARRAY_LENGTH(slow); run += 1)
        {
            const bool slowed = run >= BUSTER_ARRAY_LENGTH(counts);
            CheckProgramLanes lanes = {
                .lanes = slowed ? 8 : counts[run],
                .every_lane = true,
                .slow_lane = slowed ? slow[run - BUSTER_ARRAY_LENGTH(counts)] : 0,
                .slow_microseconds = 500,
            };
            CheckLanesTestEmission many = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, rows, unwind);
            const uint32_t bodies = unit->selected.program.body_count;
            const uint32_t expected = lanes.lanes < bodies ? lanes.lanes : bodies != 0 ? bodies : 1;
            snprintf(detail, sizeof(detail), "%s, %s path, %u lanes%s%s: status %u/%u, token %u/%u, %llu/%llu bytes, %u lanes ran", what, scalar ? "scalar" : "vector",
                     lanes.lanes, slowed ? " with a slow one" : "", rows ? ", -g and unwind tables" : "", (uint32_t)one.status, (uint32_t)many.status, one.token,
                     many.token, (unsigned long long)one.size, (unsigned long long)many.size, many.active);
            check_lanes_test_expect(tally, check_lanes_test_same(&one, &many), "an emission on lanes makes one lane's", detail);
            check_lanes_test_expect(tally, many.active == expected && (expected == 1 || many.again || many.parts > 1), "the emission runs every lane it may", detail);
            free(many.object);
            compared += 1;
        }
        free(one.object);
    }
    return compared;
}

// Generated programs (see the file's comment), written to `directory` as program_N.c, compiled by each path and emitted
// on every set of lanes.
static void check_lanes_test_programs(CheckLanesTestTally* tally, const char* directory, uint32_t programs)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const uint64_t capacity = UINT64_C(1) << 24;
    char* text = malloc(capacity);
    uint64_t emissions = 0;
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
            check_lanes_test_expect(tally, !unit.ready || emitted, "a generated program emits", path);
            functions_total += unit.ready ? unit.selected.program.body_count : 0;
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
    }
    free(text);
    printf("  programs: %u generated, %llu functions compiled, %llu emissions on lanes, each one lane's\n", programs + 2, (unsigned long long)functions_total,
           (unsigned long long)emissions);
}

// A failing program: its source and the step the first failure is at, which every lane count must report.
typedef struct CheckLanesTestFailure CheckLanesTestFailure;
struct CheckLanesTestFailure
{
    const char* name;
    const char* source;
    CheckProgramStatus status;
};

// Bodies that fail at different steps, in both orders: a lowering's refusal by name (a local _BitInt), Tier 0's
// refusal (values past its depth tables in a function with long double values), and fine bodies around them. Source
// order's lowering takes every body before Tier 0 takes any, so a lowering's refusal is the failure wherever it is.
static const CheckLanesTestFailure check_lanes_test_failures[] = {
    {
        "lowering after Tier 0",
        "long double g;\n"
        "int fine0(int a) { return a + 1; }\n"
        "int deep(int a)\n{\n    g = 1.0L;\n"
        "    return a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + a))))))))))))))));\n}\n"
        "int fine1(int a) { return a * 2; }\n"
        "int refused(int a) { _BitInt(37) x = a; return (int)x; }\n"
        "int main(void) { return fine0(1) + deep(2) + fine1(3) + refused(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
    },
    {
        "lowering before Tier 0",
        "long double g;\n"
        "int refused(int a) { _BitInt(37) x = a; return (int)x; }\n"
        "int fine0(int a) { return a + 1; }\n"
        "int deep(int a)\n{\n    g = 1.0L;\n"
        "    return a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + a))))))))))))))));\n}\n"
        "int main(void) { return fine0(1) + deep(2) + refused(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
    },
    {
        "two lowering refusals",
        "int fine0(int a) { return a + 1; }\n"
        "int refused0(int a) { _BitInt(37) x = a; return (int)x; }\n"
        "int fine1(int a) { return a * 2; }\n"
        "int refused1(int a) { _BitInt(41) y = a; return (int)y; }\n"
        "int main(void) { return fine0(1) + refused0(2) + fine1(3) + refused1(4); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
    },
    {
        "Tier 0 alone",
        "long double g;\n"
        "int fine0(int a) { return a + 1; }\n"
        "int deep(int a)\n{\n    g = 1.0L;\n"
        "    return a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + (a + a))))))))))))))));\n}\n"
        "int fine1(int a) { return a * 2; }\n"
        "int main(void) { return fine0(1) + deep(2) + fine1(3); }\n",
        CHECK_PROGRAM_UNSUPPORTED,
    },
};

// Each failing program (see the file's comment) emitted on every set of lanes: one lane's failure, at its step.
static void check_lanes_test_failing(CheckLanesTestTally* tally, const char* directory)
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
                snprintf(detail, sizeof(detail), "%s: status %u, reason %s", failure->name, (uint32_t)one.status, one.reason != NULL ? one.reason : "none");
                check_lanes_test_expect(tally, !one.emitted && one.status == failure->status, "a failing program fails at its step on one lane", detail);
                free(one.object);
                check_lanes_test_unit(tally, &unit, &parser, &pp, &identifiers, scalar != 0, failure->name, &emitted);
            }
            check_lanes_test_expect(tally, !emitted, "a failing program fails on every set of lanes", failure->name);
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
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

// The driver (see the file's comment) on four of the generated programs `program_N.c` in `directory`.
static void check_lanes_test_driver(CheckLanesTestTally* tally, const char* executable, const char* directory, uint32_t programs)
{
    static const char* const lane_counts[] = { "1", "3", "8", NULL };
    const uint32_t chosen[] = { 2, 3, 5, programs };
    char detail[PATH_MAX + 128];
    uint32_t compared = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(chosen) && chosen[i] < programs + 2; i += 1)
    {
        const uint32_t p = chosen[i];
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
        }
    }
    printf("  driver: %u objects and executables on lanes, each one lane's\n", compared);
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
    }
    char* missing[] = { (char*)executable, (char*)"-c", (char*)"-o", object, source, (char*)"--lanes", NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(missing, errors) == 1, "the driver refuses --lanes without its value", 0);
    char* joined[] = { (char*)executable, (char*)"--lanes=8", (char*)"-c", (char*)"-o", object, source, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(joined, errors) == 0, "the driver takes --lanes=8", 0);
}

static int run_check_lanes_tests(int argc, char** argv)
{
    CheckLanesTestTally tally = { .random = 0x9e3779b97f4a7c15ull };
    const char* executable = "/proc/self/exe";
    bool keep = false;
    uint32_t programs = 6;
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
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            options = false;
        }
    }
    check_lanes_test_expect(&tally, options, "options: [--executable PATH] [--seed N] [--programs N] [--keep]", 0);
    char resolved[PATH_MAX];
    const ssize_t resolved_length = strcmp(executable, "/proc/self/exe") == 0 ? readlink(executable, resolved, sizeof(resolved) - 1) : -1;
    if (resolved_length > 0)
    {
        resolved[resolved_length] = 0;
        executable = resolved;
    }
    char directory[PATH_MAX];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-lanes-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = options && mkdtemp(directory) != 0;
    check_lanes_test_expect(&tally, made, "make the test's directory", directory);
    if (made)
    {
        const bool initialized = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
        check_lanes_test_expect(&tally, initialized, "initialize the backend", 0);
        check_lanes_test_programs(&tally, directory, programs);
        check_lanes_test_failing(&tally, directory);
        check_lanes_test_rule(&tally, directory);
        check_lanes_test_driver(&tally, executable, directory, programs);
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
