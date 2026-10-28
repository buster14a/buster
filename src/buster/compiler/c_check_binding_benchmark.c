// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-checker-bindings (#336): the checker's binding handoff measured in the whole source reader. Include
// after c_check_source_benchmark.c, whose clock and median it shares. Preprocessing and parsing (the vector
// parser) happen once, outside timing. Before any timing, every mode's run on a fresh type table must publish the
// default run's acceptance, reports, declaration/type/kind/object columns and graph. Each timed sample is one
// check_input_bindings_mode call including type-table initialization and arena resets (their pages stay committed):
// reading, elaboration, the handoff, expression graph construction,
// initializer validation and history publication. Modes differ only in the handoff, so their differences are its
// cost. The isolated rows time the handoff's pieces alone on the run's retained events: the parser's grammar
// replay, both resolvers of the parser's events and both resolvers of the reader's own.
// --pipeline includes fresh parser initialization, token extraction and parsing, expression checking and parser
// cleanup as well. Preprocessing stays outside both intervals; every pipeline mode must also match the reference
// checked stream. The same invocation boundaries are used for nanoseconds and hardware cycles.
//
// Denominators: tokens; the reader's events (declarations, uses and restores, with parameter reactivations
// counted as declarations); the distinct names those events bind; and the parser replay's events. Memory: the
// output arena's retained bytes, the highest byte each arena's run writes (a lower bound on its peak, found by
// poisoning before an untimed run), and each isolated piece's own footprint past its arenas' lengths.
//
// Besides a file, it takes built-in inputs (--input NAME; --print writes one out): small and large, the 4 and
// 4,096 functions of the checker's source-measurement generator (the same bytes, whose SHA-256 are be568cb5...
// and 0e33678843...), and headers, a header-heavy unit: most of its tokens and binding events come from 24 C
// library and POSIX headers, as a typical translation unit's do, and four functions use and shadow them.
#ifndef BUSTER_C_CHECK_BINDING_BENCHMARK_INCLUDED
#define BUSTER_C_CHECK_BINDING_BENCHMARK_INCLUDED

#define BUSTER_CHECK_BINDING_BENCHMARK_POISON 0x5au

static const char check_binding_benchmark_headers[] =
    "#define _GNU_SOURCE 1\n"
    "#include <assert.h>\n"
    "#include <ctype.h>\n"
    "#include <errno.h>\n"
    "#include <fcntl.h>\n"
    "#include <float.h>\n"
    "#include <inttypes.h>\n"
    "#include <limits.h>\n"
    "#include <locale.h>\n"
    "#include <math.h>\n"
    "#include <pthread.h>\n"
    "#include <signal.h>\n"
    "#include <stdarg.h>\n"
    "#include <stdbool.h>\n"
    "#include <stddef.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <sys/mman.h>\n"
    "#include <sys/stat.h>\n"
    "#include <sys/types.h>\n"
    "#include <time.h>\n"
    "#include <unistd.h>\n"
    "#include <wchar.h>\n"
    "\n"
    "typedef struct Buffer Buffer;\n"
    "struct Buffer\n"
    "{\n"
    "    char* bytes;\n"
    "    size_t length;\n"
    "    size_t capacity;\n"
    "};\n"
    "\n"
    "static int count_digits(const char* text, size_t length)\n"
    "{\n"
    "    int count = 0;\n"
    "    for (size_t i = 0; i < length; i += 1)\n"
    "    {\n"
    "        count += isdigit((unsigned char)text[i]) != 0;\n"
    "    }\n"
    "    return count;\n"
    "}\n"
    "\n"
    "static bool buffer_append(Buffer* buffer, const char* text)\n"
    "{\n"
    "    const size_t length = strlen(text);\n"
    "    bool fits = buffer->length + length <= buffer->capacity;\n"
    "    if (fits)\n"
    "    {\n"
    "        memcpy(buffer->bytes + buffer->length, text, length);\n"
    "        buffer->length += length;\n"
    "    }\n"
    "    return fits;\n"
    "}\n"
    "\n"
    "int describe(const char* path, Buffer* buffer)\n"
    "{\n"
    "    struct stat status;\n"
    "    int result = stat(path, &status);\n"
    "    if (result == 0)\n"
    "    {\n"
    "        char line[128];\n"
    "        int length = snprintf(line, sizeof line, \"%\" PRIu64 \" bytes, mode %o\\n\", (uint64_t)status.st_size, (unsigned)status.st_mode);\n"
    "        {\n"
    "            int result = count_digits(line, (size_t)length);\n"
    "            length = result > 0 ? length : 0;\n"
    "        }\n"
    "        result = buffer_append(buffer, line) ? 0 : ENOSPC;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        result = errno;\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "\n"
    "double scaled(double value, int exponent)\n"
    "{\n"
    "    double result = ldexp(value, exponent);\n"
    "    return isfinite(result) ? result : DBL_MAX;\n"
    "}\n";

// The input named `name`, in `arena`: the generator's functions, or the header-heavy unit. 0 for another name.
static char* check_binding_benchmark_input(Arena* arena, const char* name, uint32_t* length)
{
    const uint32_t functions = strcmp(name, "small") == 0 ? 4 : strcmp(name, "large") == 0 ? 4096 : 0;
    const bool headers = strcmp(name, "headers") == 0;
    const uint64_t capacity = headers ? sizeof(check_binding_benchmark_headers) : 64 + (uint64_t)functions * 192;
    char* text = functions != 0 || headers ? BUSTER_ARENA_PUSH_ARRAY(arena, char, capacity) : 0;
    uint32_t used = 0;
    if (text != 0 && headers)
    {
        memcpy(text, check_binding_benchmark_headers, sizeof(check_binding_benchmark_headers));
        used = (uint32_t)sizeof(check_binding_benchmark_headers) - 1;
    }
    else if (text != 0)
    {
        used = (uint32_t)snprintf(text, capacity, "typedef unsigned long Word;\nstatic Word seed = 7;\n");
        for (uint32_t i = 0; i < functions; i += 1)
        {
            used += (uint32_t)snprintf(text + used, capacity - used,
                                       "Word f%u(Word a, Word b) { Word x = a + %uu; { Word a = b ^ %uu; x = x + a; } if (x < b) x = x + seed; "
                                       "return (x * 3u) ^ (b >> 1); }\n", i, i + 1, i + 3);
        }
    }
    *length = used;
    return text;
}

typedef struct CheckBindingBenchmark CheckBindingBenchmark;
struct CheckBindingBenchmark
{
    Parser* parser;
    Preprocessor* pp;
    TypeCapacity capacity;
    Arena types;
    Arena index;
    Arena output;
    Arena scratch;
    TypeTable table;
    CheckStream stream;
    uint64_t parser_bytes;
    uint64_t type_bytes;
    uint64_t retained_bytes;
    const CheckInput* reference;
    const CheckStream* reference_stream;
    bool pipeline;
};

static bool check_binding_benchmark_equal(const CheckInput* left, const CheckInput* right);

static bool check_binding_benchmark_run(CheckBindingBenchmark* benchmark, CheckInputBindingMode mode, CheckInput* input, uint64_t* nanoseconds)
{
    const uint64_t begin = check_source_time();
    *input = (CheckInput){ 0 };
    benchmark->stream = (CheckStream){ 0 };
    Parser fresh = { 0 };
    Parser* parser = benchmark->pipeline ? &fresh : benchmark->parser;
    if (benchmark->pipeline)
    {
        benchmark->types = arena_reserve(type_capacity_bytes(benchmark->capacity));
        benchmark->index = arena_reserve(type_index_capacity_bytes(benchmark->capacity));
        benchmark->output = arena_reserve(UINT64_C(1) << 36);
        benchmark->scratch = arena_reserve(UINT64_C(1) << 36);
    }
    bool valid = benchmark->types.base != 0 && benchmark->index.base != 0 && benchmark->output.base != 0 && benchmark->scratch.base != 0;
    valid = valid && (!benchmark->pipeline || (parse_initialize(parser, &benchmark->parser->options, benchmark->pp->options.language) &&
                     parse_translation_unit(parser, benchmark->pp) && parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE));
    arena_pop_to(&benchmark->types, 0);
    arena_pop_to(&benchmark->index, 0);
    arena_pop_to(&benchmark->output, 0);
    arena_pop_to(&benchmark->scratch, 0);
    valid = valid && type_initialize(&benchmark->table, &benchmark->types, &benchmark->index, benchmark->capacity);
    valid = valid && check_input_bindings_mode(input, parser, benchmark->pp, &benchmark->table, &benchmark->output, &benchmark->scratch, mode);
    if (valid && benchmark->pipeline)
    {
        const CheckOptions options = { 0 };
        valid = check_graph_run(&benchmark->table, &input->graph, &benchmark->output, &benchmark->scratch, &options, &benchmark->stream) &&
                benchmark->stream.status == CHECK_STATUS_OK && benchmark->stream.report_count == 0;
    }
    benchmark->parser_bytes = parser->scratch.length + parser->gnu_arena.length;
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(parser->columns); column += 1)
    {
        benchmark->parser_bytes += parser->columns[column].length;
    }
    benchmark->type_bytes = benchmark->types.length + benchmark->index.length;
    benchmark->retained_bytes = benchmark->output.length;
    // Only the untimed verification calls supply a reference. Compare while the fresh arenas are alive, then
    // return scalar metadata rather than pointers into released storage. Timed samples perform no comparison.
    if (valid && benchmark->pipeline && benchmark->reference != 0)
    {
        valid = check_binding_benchmark_equal(benchmark->reference, input) &&
                check_test_stream_equal(benchmark->reference_stream, &benchmark->stream, input->graph.argument_count);
    }
    if (benchmark->pipeline)
    {
        const CheckInput summary = {
            .graph = { .count = input->graph.count },
            .environment = { .valid = input->environment.valid },
            .report_count = input->report_count,
            .token_count = input->token_count,
            .binding_statistics = input->binding_statistics,
            .binding_event_count = input->binding_event_count,
            .parser_binding_statistics = input->parser_binding_statistics,
            .parser_binding_event_count = input->parser_binding_event_count,
            .resource = input->resource,
        };
        parse_release(&fresh);
        arena_release(&benchmark->scratch);
        arena_release(&benchmark->output);
        arena_release(&benchmark->index);
        arena_release(&benchmark->types);
        benchmark->stream = (CheckStream){ 0 };
        benchmark->table = (TypeTable){ 0 };
        *input = summary;
    }
    *nanoseconds = check_source_time() - begin;
    return valid && !input->resource;
}

static void check_binding_benchmark_poison(Arena* arena, uint64_t from, uint64_t probe)
{
    memset(arena->base + from, BUSTER_CHECK_BINDING_BENCHMARK_POISON, probe - from);
}

// How far past `from`, below `probe`, the bytes were written since check_binding_benchmark_poison.
static uint64_t check_binding_benchmark_touched(const Arena* arena, uint64_t from, uint64_t probe)
{
    uint64_t top = probe;
    while (top > from && arena->base[top - 1] == BUSTER_CHECK_BINDING_BENCHMARK_POISON)
    {
        top -= 1;
    }
    return top - from;
}

// The highest byte of each arena that the run wrote, doubling the poisoned range until the run's writes stay below
// it. This is a lower bound on the arena's peak: pushes never written, and transient ones that later retained
// pushes write over, don't show. The isolated pieces' footprints below are exact for what they write.
static bool check_binding_benchmark_peaks(CheckBindingBenchmark* benchmark, CheckInputBindingMode mode, uint64_t* output_peak, uint64_t* scratch_peak)
{
    bool valid = true;
    bool contained = false;
    for (uint64_t probe = UINT64_C(1) << 26; valid && !contained; probe *= 2)
    {
        Arena* arenas[] = {
            &benchmark->output,
            &benchmark->scratch,
        };
        valid = probe <= benchmark->output.capacity && probe <= benchmark->scratch.capacity;
        for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
        {
            check_binding_benchmark_poison(arenas[i], 0, probe);
        }
        CheckInput input;
        uint64_t nanoseconds = 0;
        valid = valid && check_binding_benchmark_run(benchmark, mode, &input, &nanoseconds);
        uint64_t* peaks[] = {
            output_peak,
            scratch_peak,
        };
        contained = valid;
        for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
        {
            *peaks[i] = check_binding_benchmark_touched(arenas[i], 0, probe);
            contained &= *peaks[i] < probe;
        }
    }
    return valid;
}

static bool check_binding_benchmark_equal(const CheckInput* left, const CheckInput* right)
{
    bool equal = left->token_count == right->token_count && left->report_count == right->report_count && left->graph.count == right->graph.count &&
                 left->binding_event_count == right->binding_event_count && left->environment.valid == right->environment.valid &&
                 left->binding_statistics.identifiers == right->binding_statistics.identifiers &&
                 left->binding_statistics.declarations == right->binding_statistics.declarations;
    for (uint32_t token = 0; equal && token <= left->token_count; token += 1)
    {
        equal = left->declarations[token] == right->declarations[token] && left->declared_types[token] == right->declared_types[token] &&
                left->declaration_kinds[token] == right->declaration_kinds[token] && left->objects[token] == right->objects[token] &&
                left->linkage[token] == right->linkage[token] && left->defined[token] == right->defined[token] &&
                left->token_nodes[token] == right->token_nodes[token];
    }
    for (uint32_t report = 0; equal && report < left->report_count; report += 1)
    {
        equal = left->reports[report].token == right->reports[report].token && left->reports[report].message == right->reports[report].message;
    }
    for (uint32_t node = 0; equal && node < left->graph.count; node += 1)
    {
        equal = left->graph.ops[node] == right->graph.ops[node] && left->graph.tokens[node] == right->graph.tokens[node] &&
                left->graph.operand0[node] == right->graph.operand0[node] && left->graph.operand1[node] == right->graph.operand1[node] &&
                left->graph.operand2[node] == right->graph.operand2[node];
    }
    return equal;
}

static double check_binding_benchmark_per(uint64_t value, uint64_t denominator)
{
    return denominator != 0 ? (double)value / (double)denominator : 0.0;
}

static uint64_t check_binding_benchmark_extreme(const uint64_t* values, uint32_t count, bool maximum)
{
    uint64_t result = count != 0 ? values[0] : 0;
    for (uint32_t i = 1; i < count; i += 1)
    {
        result = maximum ? (values[i] > result ? values[i] : result) : (values[i] < result ? values[i] : result);
    }
    return result;
}

// Measures the unit `file` names, or `input_text` under that name: the checks, then the samples and the report.
static bool check_binding_benchmark_measure(PPOptions* pp_options, const char* file, const char* input_text, uint32_t input_length, uint64_t passes,
                                            uint64_t after, uint32_t only, bool pipeline)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    pp_options->semantic_pragmas = true;
    const ParseOptions parse_options = { 0 };
    const uint64_t preprocessing_begin = check_source_time();
    bool valid = intern_initialize(&identifiers) && pp_initialize(&pp, pp_options, &identifiers) &&
            (input_text != 0 ? pp_run_source(&pp, file, (const uint8_t*)input_text, input_length) : pp_run(&pp, file));
    const uint64_t preprocessing_ns = check_source_time() - preprocessing_begin;
    valid = valid && parse_initialize(&parser, &parse_options, pp.options.language) && parse_translation_unit(&parser, &pp) &&
            parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE;
    const uint32_t tokens = parser.tokens.count;
    CheckBindingBenchmark benchmark = {
        .parser = &parser,
        .pp = &pp,
        .pipeline = pipeline,
        .capacity = {
            .types = tokens * 4 + 64,
            .parameters = tokens + 1,
            .tags = tokens + 1,
            .members = tokens + 1,
        },
    };
    valid = valid && tokens < (1u << 27);
    if (valid && !pipeline)
    {
        benchmark.types = arena_reserve(type_capacity_bytes(benchmark.capacity));
        benchmark.index = arena_reserve(type_index_capacity_bytes(benchmark.capacity));
        benchmark.output = arena_reserve(UINT64_C(1) << 36);
        benchmark.scratch = arena_reserve(UINT64_C(1) << 36);
    }
    // The reference run's columns, kept in arenas of their own.
    Arena reference_output = arena_reserve(UINT64_C(1) << 36);
    Arena reference_scratch = arena_reserve(UINT64_C(1) << 36);
    Arena reference_types = valid ? arena_reserve(type_capacity_bytes(benchmark.capacity)) : (Arena){ 0 };
    Arena reference_index = valid ? arena_reserve(type_index_capacity_bytes(benchmark.capacity)) : (Arena){ 0 };
    TypeTable reference_table;
    CheckInput reference = { 0 };
    valid = valid && (pipeline || (benchmark.types.base != 0 && benchmark.index.base != 0 && benchmark.output.base != 0 && benchmark.scratch.base != 0)) &&
            reference_output.base != 0 && reference_scratch.base != 0 &&
            type_initialize(&reference_table, &reference_types, &reference_index, benchmark.capacity);
    const bool accepted = valid && check_input_bindings_mode(&reference, &parser, &pp, &reference_table, &reference_output, &reference_scratch, CHECK_INPUT_BINDINGS_DEFAULT);
    valid = valid && !reference.resource;
    if (valid && !accepted)
    {
        fprintf(stderr, "benchmark-checker-bindings: %s: the checker rejects the unit (%u reports); measure an accepted one\n", file, reference.report_count);
    }
    valid = valid && accepted && reference.environment.valid;
    CheckStream reference_stream = { 0 };
    if (valid && pipeline)
    {
        const CheckOptions options = { 0 };
        valid = check_graph_run(&reference_table, &reference.graph, &reference_output, &reference_scratch, &options, &reference_stream) &&
                reference_stream.status == CHECK_STATUS_OK && reference_stream.report_count == 0;
    }
    benchmark.reference = &reference;
    benchmark.reference_stream = &reference_stream;
    uint32_t parser_events = 0;
    CBindingStatistics statistics[CHECK_INPUT_BINDING_MODES] = { 0 };
    // The tokens each mode's walks visited (#529): the walk in source order's and the expressions' walk's.
    uint32_t visits[CHECK_INPUT_BINDING_MODES][2] = { { 0 } };
    for (uint32_t mode = CHECK_INPUT_BINDINGS_DIRECT; valid && mode < CHECK_INPUT_BINDING_MODES; mode += 1)
    {
        CheckInput input;
        uint64_t nanoseconds = 0;
        valid = check_binding_benchmark_run(&benchmark, (CheckInputBindingMode)mode, &input, &nanoseconds) &&
                (pipeline || check_binding_benchmark_equal(&reference, &input));
        const bool parser_mode = mode == CHECK_INPUT_BINDINGS_PARSER_STACKS || mode == CHECK_INPUT_BINDINGS_PARSER_BATCH;
        parser_events = parser_mode ? input.parser_binding_event_count : parser_events;
        statistics[mode] = parser_mode ? input.parser_binding_statistics : input.binding_statistics;
        visits[mode][0] = input.resolve_visits;
        visits[mode][1] = input.expression_visits;
        if (!valid)
        {
            fprintf(stderr, "benchmark-checker-bindings: %s: mode %s differs from the default handoff\n", file, check_input_binding_mode_names[mode]);
        }
    }
    benchmark.reference = 0;
    benchmark.reference_stream = 0;
    // The reader's retained events: what they bind, and how many distinct names.
    const CBindings* events = valid ? reference.environment.bindings : 0;
    uint64_t uses = 0;
    uint64_t declarations = 0;
    uint64_t restores = 0;
    uint64_t distinct = 0;
    uint64_t max_name_events = 0;
    uint64_t active_peak = 0;
    uint64_t active = 0;
    uint32_t* seen = valid ? BUSTER_ARENA_PUSH_ARRAY(&reference_scratch, uint32_t, (uint64_t)events->name_capacity * C_BINDING_NAMESPACES) : 0;
    valid = valid && seen != 0;
    if (valid)
    {
        memset(seen, 0, (uint64_t)events->name_capacity * C_BINDING_NAMESPACES * sizeof(uint32_t));
        for (uint32_t event = 0; event < events->count; event += 1)
        {
            const uint8_t operation = events->operation[event];
            uses += operation == C_BINDING_USE;
            declarations += operation == C_BINDING_DECLARE;
            restores += operation == C_BINDING_RESTORE;
            active = operation == C_BINDING_RESTORE ? events->declaration[event] : active + (operation == C_BINDING_DECLARE);
            active_peak = active > active_peak ? active : active_peak;
            if (operation != C_BINDING_RESTORE)
            {
                distinct += seen[events->key[event]] == 0;
                const uint32_t occupancy = ++seen[events->key[event]];
                max_name_events = occupancy > max_name_events ? occupancy : max_name_events;
            }
        }
    }
    uint64_t retained[CHECK_INPUT_BINDING_MODES] = { 0 };
    uint64_t output_peaks[CHECK_INPUT_BINDING_MODES] = { 0 };
    uint64_t scratch_peaks[CHECK_INPUT_BINDING_MODES] = { 0 };
    for (uint32_t mode = CHECK_INPUT_BINDINGS_DIRECT; valid && mode < CHECK_INPUT_BINDING_MODES; mode += 1)
    {
        if (pipeline)
        {
            CheckInput input;
            uint64_t nanoseconds = 0;
            valid = check_binding_benchmark_run(&benchmark, (CheckInputBindingMode)mode, &input, &nanoseconds);
        }
        else
        {
            valid = check_binding_benchmark_peaks(&benchmark, (CheckInputBindingMode)mode, output_peaks + mode, scratch_peaks + mode);
        }
        retained[mode] = benchmark.retained_bytes;
    }
    const struct perf_event_attr attribute = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    const int counter = valid ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, 0) : -1;
    bool counters = counter >= 0;
    static uint64_t nanoseconds[CHECK_INPUT_BINDING_MODES][BUSTER_CHECK_SOURCE_MAX_PASSES];
    static uint64_t cycles[CHECK_INPUT_BINDING_MODES][BUSTER_CHECK_SOURCE_MAX_PASSES];
    const uint32_t first = only != CHECK_INPUT_BINDING_MODES ? only : CHECK_INPUT_BINDINGS_DIRECT;
    const uint32_t modes = only != CHECK_INPUT_BINDING_MODES ? 1 : CHECK_INPUT_BINDING_MODES - CHECK_INPUT_BINDINGS_DIRECT;
    // One warm-up pass, then passes whose order rotates so that no mode always follows the same one.
    for (uint32_t pass = 0; valid && pass <= passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < modes; step += 1)
        {
            const uint32_t mode = first + (pass + step) % modes;
            CheckInput input;
            uint64_t elapsed = 0;
            if (after != 0)
            {
                // The lighter phase before either scope: the scalar twin's reader, without the vector parser
                // or expression checker. Keep this conditioning independent of the timed pipeline option.
                const uint64_t until = check_source_time() + after * 1000000;
                benchmark.pipeline = false;
                if (pipeline)
                {
                    benchmark.types = arena_reserve(type_capacity_bytes(benchmark.capacity));
                    benchmark.index = arena_reserve(type_index_capacity_bytes(benchmark.capacity));
                    benchmark.output = arena_reserve(UINT64_C(1) << 36);
                    benchmark.scratch = arena_reserve(UINT64_C(1) << 36);
                }
                while (valid && check_source_time() < until)
                {
                    valid = check_binding_benchmark_run(&benchmark, CHECK_INPUT_BINDINGS_STACKS, &input, &elapsed);
                }
                if (pipeline)
                {
                    arena_release(&benchmark.scratch);
                    arena_release(&benchmark.output);
                    arena_release(&benchmark.index);
                    arena_release(&benchmark.types);
                }
                benchmark.pipeline = pipeline;
            }
            if (counter >= 0)
            {
                counters &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
            }
            valid = valid && check_binding_benchmark_run(&benchmark, (CheckInputBindingMode)mode, &input, &elapsed);
            uint64_t counts[3] = { 0 };
            if (counter >= 0)
            {
                counters &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts) &&
                            counts[1] != 0 && counts[1] == counts[2];
            }
            if (pass != 0)
            {
                nanoseconds[mode][pass - 1] = elapsed;
                cycles[mode][pass - 1] = counts[0];
            }
        }
    }
    // The handoff's pieces alone, on the retained events: the reader's (both resolvers) and the parser's replay.
    enum
    {
        ISOLATED_READER_STACKS,
        ISOLATED_READER_BATCH,
        ISOLATED_PARSER_REPLAY,
        ISOLATED_PARSER_STACKS,
        ISOLATED_PARSER_BATCH,
        ISOLATED_COUNT,
    };
    static const char* const isolated_names[ISOLATED_COUNT] = {
        "reader-stacks",
        "reader-batch",
        "parser-replay",
        "parser-resolve-stacks",
        "parser-resolve-batch",
    };
    static uint64_t isolated[ISOLATED_COUNT][BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t footprints[ISOLATED_COUNT][2] = { 0 };
    // Every piece's footprint is below the whole run's.
    uint64_t bound = 0;
    for (uint32_t mode = CHECK_INPUT_BINDINGS_DIRECT; mode < CHECK_INPUT_BINDING_MODES; mode += 1)
    {
        bound = output_peaks[mode] > bound ? output_peaks[mode] : bound;
        bound = scratch_peaks[mode] > bound ? scratch_peaks[mode] : bound;
        bound = retained[mode] > bound ? retained[mode] : bound;
    }
    bound = 2 * bound + 4096;
    uint32_t* results = valid && !pipeline && only == CHECK_INPUT_BINDING_MODES ? BUSTER_ARENA_PUSH_ARRAY(&reference_scratch, uint32_t, (uint64_t)events->count + 1) : 0;
    for (uint32_t pass = 0; results != 0 && valid && pass <= passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < ISOLATED_COUNT; step += 1)
        {
            const uint32_t piece = (pass + step) % ISOLATED_COUNT;
            arena_pop_to(&benchmark.output, 0);
            arena_pop_to(&benchmark.scratch, 0);
            Parser view = parser;
            view.scratch = benchmark.scratch;
            ParseBindings parsed = { 0 };
            CBindingStatistics ignored = { 0 };
            const bool replayed = piece < ISOLATED_PARSER_STACKS || parse_bindings_build(&parsed, &view, &benchmark.output);
            const uint64_t from[] = {
                benchmark.output.length,
                benchmark.scratch.length,
            };
            if (pass == 0)
            {
                // The warm-up sample finds the piece's footprint: what it writes past each arena's length.
                check_binding_benchmark_poison(&benchmark.output, from[0], from[0] + bound);
                check_binding_benchmark_poison(&benchmark.scratch, from[1], from[1] + bound);
            }
            const uint64_t begin = check_source_time();
            valid = replayed &&
                    (piece == ISOLATED_READER_STACKS ? c_bindings_scalar(events, results, &benchmark.scratch) :
                     piece == ISOLATED_READER_BATCH ? c_bindings_batch(events, results, &benchmark.scratch, &ignored) :
                     piece == ISOLATED_PARSER_REPLAY ? parse_bindings_build(&parsed, &view, &benchmark.output) :
                     parse_bindings_resolve(&parsed, piece == ISOLATED_PARSER_STACKS, &benchmark.scratch));
            const uint64_t elapsed = check_source_time() - begin;
            if (pass != 0)
            {
                isolated[piece][pass - 1] = elapsed;
            }
            else
            {
                footprints[piece][0] = check_binding_benchmark_touched(&benchmark.output, from[0], from[0] + bound);
                footprints[piece][1] = check_binding_benchmark_touched(&benchmark.scratch, from[1], from[1] + bound);
            }
        }
    }
    if (valid)
    {
        const uint32_t samples = (uint32_t)passes;
        printf("checker-binding-benchmark: source=%s passes=%u after_scalar_ms=%" PRIu64 " scope=%s comparison=identical\n",
               file, samples, after, pipeline ? "parse-reader-check-cleanup" : "reader-initialize");
        printf("denominators: tokens=%u reader_events=%u declarations=%" PRIu64 " uses=%" PRIu64 " restores=%" PRIu64 " distinct_names=%" PRIu64
               " parser_events=%u name_capacity=%u max_name_events=%" PRIu64 " active_declarations_peak=%" PRIu64 "\n",
               tokens, events->count, declarations, uses, restores, distinct, parser_events, events->name_capacity, max_name_events, active_peak);
        printf("shared-memory: parser_retained_bytes=%" PRIu64 " type_retained_bytes=%" PRIu64 "\n", benchmark.parser_bytes, benchmark.type_bytes);
        const uint64_t direct = check_source_median(nanoseconds[CHECK_INPUT_BINDINGS_DIRECT], samples);
        for (uint32_t mode = first; mode < first + modes; mode += 1)
        {
            const uint64_t median = check_source_median(nanoseconds[mode], samples);
            const uint64_t median_cycles = counters ? check_source_median(cycles[mode], samples) : 0;
            printf("%s: mode=%s ns=%" PRIu64 " min_ns=%" PRIu64 " max_ns=%" PRIu64 " cycles=%" PRIu64 " ns_per_token=%.3f ns_per_event=%.3f"
                   " ns_per_use=%.3f ns_per_distinct_name=%.3f cycles_per_event=%.3f",
                   pipeline ? "pipeline" : "check_input", check_input_binding_mode_names[mode], median, check_binding_benchmark_extreme(nanoseconds[mode], samples, false),
                   check_binding_benchmark_extreme(nanoseconds[mode], samples, true), median_cycles, check_binding_benchmark_per(median, tokens),
                   check_binding_benchmark_per(median, events->count), check_binding_benchmark_per(median, uses),
                   check_binding_benchmark_per(median, distinct), check_binding_benchmark_per(median_cycles, events->count));
            if (only == CHECK_INPUT_BINDING_MODES)
            {
                printf(" over_direct_ns=%" PRId64, (int64_t)median - (int64_t)direct);
            }
            printf(" walked_uses=%" PRIu64 " fallback_uses=%" PRIu64 " resolve_visits=%u expression_visits=%u retained_bytes=%" PRIu64
                   " written_bytes_available=%u output_written_bytes=%" PRIu64 " scratch_written_bytes=%" PRIu64 "\n",
                   statistics[mode].walked_uses, statistics[mode].fallback_uses, visits[mode][0], visits[mode][1], retained[mode], !pipeline,
                   output_peaks[mode], scratch_peaks[mode]);
        }
        for (uint32_t piece = 0; results != 0 && piece < ISOLATED_COUNT; piece += 1)
        {
            const uint64_t median = check_source_median(isolated[piece], samples);
            const uint32_t denominator = piece < ISOLATED_PARSER_REPLAY ? events->count : parser_events;
            printf("isolated: piece=%s ns=%" PRIu64 " min_ns=%" PRIu64 " max_ns=%" PRIu64 " ns_per_event=%.3f output_bytes=%" PRIu64 " scratch_bytes=%" PRIu64 "\n",
                   isolated_names[piece], median, check_binding_benchmark_extreme(isolated[piece], samples, false),
                   check_binding_benchmark_extreme(isolated[piece], samples, true), check_binding_benchmark_per(median, denominator),
                   footprints[piece][0], footprints[piece][1]);
        }
    }
    if (counter >= 0)
    {
        close(counter);
    }
    arena_release(&reference_index);
    arena_release(&reference_types);
    arena_release(&reference_scratch);
    arena_release(&reference_output);
    arena_release(&benchmark.scratch);
    arena_release(&benchmark.output);
    arena_release(&benchmark.index);
    arena_release(&benchmark.types);
    parse_release(&parser);
    const uint64_t cleanup_begin = check_source_time();
    pp_release(&pp);
    intern_release(&identifiers);
    if (valid)
    {
        printf("common-preprocessing: initialization_and_run_ns=%" PRIu64 " cleanup_ns=%" PRIu64 " samples=1 excluded_from_timed_scope=1\n",
               preprocessing_ns, check_source_time() - cleanup_begin);
    }
    return valid;
}

static int run_checker_binding_benchmark(int count, char** arguments)
{
    uint64_t passes = 21;
    uint64_t after = 0;
    uint32_t only = CHECK_INPUT_BINDING_MODES;
    const char* input_name = 0;
    bool print = false;
    bool pipeline = false;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char** own = BUSTER_ARENA_PUSH_ARRAY(&option_arena, char*, (uint64_t)count + 1);
    uint32_t used = 0;
    bool valid = own != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        const char* option = arguments[i];
        if (strcmp(option, "--print") == 0)
        {
            print = true;
        }
        else if (strcmp(option, "--pipeline") == 0)
        {
            pipeline = true;
        }
        else if (strcmp(option, "--only") == 0 || strcmp(option, "--passes") == 0 || strcmp(option, "--after-scalar") == 0 || strcmp(option, "--input") == 0)
        {
            valid = i + 1 < count;
            const char* value = valid ? arguments[++i] : "";
            if (strcmp(option, "--input") == 0)
            {
                // The preprocessor's options take it as the unit's name, so that a file besides it is refused.
                input_name = value;
                own[used++] = arguments[i];
            }
            else if (strcmp(option, "--only") == 0)
            {
                only = CHECK_INPUT_BINDING_MODES;
                for (uint32_t mode = CHECK_INPUT_BINDINGS_DIRECT; mode < CHECK_INPUT_BINDING_MODES; mode += 1)
                {
                    only = strcmp(value, check_input_binding_mode_names[mode]) == 0 ? mode : only;
                }
                valid = valid && only != CHECK_INPUT_BINDING_MODES;
            }
            else
            {
                valid = valid && check_corpus_number(value, strcmp(option, "--passes") == 0 ? &passes : &after);
            }
        }
        else if (strcmp(option, "--no-default-config") != 0)
        {
            own[used++] = arguments[i];
        }
    }
    PPOptions pp_options;
    const char* file = 0;
    const char* output_file = 0;
    bool markers = false;
    valid = valid && passes != 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES && after <= 1000 && (after == 0 || only != CHECK_INPUT_BINDING_MODES) &&
            pp_parse_options((int)used, own, &pp_options, &option_arena, &file, &output_file, &markers) && output_file == 0 &&
            (!print || input_name != 0);
    uint32_t input_length = 0;
    const char* input_text = valid && input_name != 0 ? check_binding_benchmark_input(&option_arena, input_name, &input_length) : 0;
    valid = valid && (input_name == 0 || input_text != 0);
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-checker-bindings [--pipeline] [--passes N] [--only MODE [--after-scalar MS]] [-E's options] {file.c | --input NAME}\n"
                        "       benchmark-checker-bindings --input NAME --print\n"
                        "modes: direct, stacks, batch, parser-stacks, parser-batch; inputs: small, large, headers\n");
    }
    if (valid && print)
    {
        valid = fwrite(input_text, 1, input_length, stdout) == input_length;
    }
    else if (valid)
    {
        valid = check_binding_benchmark_measure(&pp_options, file, input_text, input_length, passes, after, only, pipeline);
    }
    arena_release(&option_arena);
    return valid ? 0 : 1;
}

#endif
