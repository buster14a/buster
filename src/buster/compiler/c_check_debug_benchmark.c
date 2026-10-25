// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-check-debug [--functions N] [--seed N] [--runs N] [--only rows|lines|keep|build|emit] [--path vector|scalar]
// [--after-scalar MS] [--print PATH]: the C driver's source rows (#395, c_check_debug.c), each pass by its vector path
// and its exact twin. --print writes the generated program to PATH and measures nothing, for timing whole compiles of
// it with -g and without. Include after c_check_debug_test.c, whose generator and pipeline it takes, and c_check_source_benchmark.c, whose
// clock and median it shares.
//
// - rows: the raw rows of synthetic functions shaped like Tier 0's (entries of 0 to 3 records, about a third fused into
//   their consumers, records of 1 to 15 bytes), 300 entries each, --functions of them (2,000), per entry;
// - lines, keep and build: a generated program of --functions functions (the test's statements: macros, some across
//   lines; no #line, as most C has none), compiled through the pipeline with source rows once, outside the timing; per
//   raw row, each function's rows' lines (check_debug_lines), the rows kept from them (check_debug_keep) and the whole
//   unit (check_debug_build: lines, kept rows and debug_write);
// - emit: check_program_emit of that program with the raw rows and without, by the vector path, per typed entry: what
//   -g adds to the emission (the entries' tokens copied out of the lowering and the raw rows).
//
// Before timing, each pass's paths must give the same rows, lines, kept rows and sections. Each sample takes the pass
// over every function once; the report gives the medians of --runs (21) runs, the paths taking turns. --path times one
// path alone, back to back, and --after-scalar MS runs the exact twin for MS ms before each sample of the vector path
// (the state a compiler's vector phases run in after lighter code, #199). Run it under the 9700X's lock with taskset -c 2.

#define BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES 300

typedef enum CheckDebugBenchmarkPass
{
    CHECK_DEBUG_BENCHMARK_ROWS,
    CHECK_DEBUG_BENCHMARK_LINES,
    CHECK_DEBUG_BENCHMARK_KEEP,
    CHECK_DEBUG_BENCHMARK_BUILD,
    CHECK_DEBUG_BENCHMARK_EMIT,
    CHECK_DEBUG_BENCHMARK_PASSES,
} CheckDebugBenchmarkPass;

static const char* const check_debug_benchmark_names[CHECK_DEBUG_BENCHMARK_PASSES] = { "rows", "lines", "keep", "build", "emit" };

// The synthetic functions of the rows pass: entry e of function f's first record and token, its records' offsets.
typedef struct CheckDebugBenchmarkRows CheckDebugBenchmarkRows;
struct CheckDebugBenchmarkRows
{
    uint32_t* entry_records;
    uint32_t* tokens;
    uint32_t* offsets;
    uint32_t* offset_first;
    uint32_t* lengths;
    uint32_t* row_offsets[2];
    uint32_t* row_tokens[2];
    uint32_t functions;
};

// Everything the passes after the rows take: the compiled program and the outputs of each path.
typedef struct CheckDebugBenchmark CheckDebugBenchmark;
struct CheckDebugBenchmark
{
    CheckDebugTestUnit unit;
    Preprocessor* pp;
    Parser* parser;
    InternTable* identifiers;
    CheckDebugSource source;
    CheckDebugBenchmarkRows synthetic;
    uint32_t* closings;
    uint32_t* keys[2];
    uint32_t* lines[2];
    uint32_t* kept_offsets[2];
    uint32_t* kept_lines[2];
    uint64_t kept[2];
};

// The rows pass over every synthetic function by a path; returns the rows.
static uint64_t check_debug_benchmark_rows(CheckDebugBenchmarkRows* rows, uint32_t path)
{
    uint64_t count = 0;
    for (uint32_t f = 0; f < rows->functions; f += 1)
    {
        const uint32_t* entry_records = rows->entry_records + (uint64_t)f * (BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES + 1);
        const uint32_t* tokens = rows->tokens + (uint64_t)f * BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES;
        const uint32_t* offsets = rows->offsets + rows->offset_first[f];
        uint32_t* row_offsets = rows->row_offsets[path] + count;
        uint32_t* row_tokens = rows->row_tokens[path] + count;
        count += path == 0 ? check_debug_rows_scalar(entry_records, tokens, BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES, offsets, rows->lengths[f], 1, 2, row_offsets, row_tokens) :
                             check_debug_rows(entry_records, tokens, BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES, offsets, rows->lengths[f], 1, 2, row_offsets, row_tokens);
    }
    return count;
}

// The lines pass over every function's raw rows by a path.
static void check_debug_benchmark_lines(CheckDebugBenchmark* benchmark, uint32_t path)
{
    const CheckProgram* program = &benchmark->unit.selected.program;
    const CheckProgramRows* rows = &benchmark->unit.rows;
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const uint32_t first = rows->first[b];
        check_debug_lines(&benchmark->source, program->bodies[b].declaration, benchmark->closings[b], rows->tokens + first, rows->counts[b],
                          benchmark->keys[path] + first, benchmark->lines[path] + first, path == 0);
    }
}

// The kept rows' pass over every function's rows by a path, from the exact path's lines; returns the rows kept.
static uint64_t check_debug_benchmark_keep(CheckDebugBenchmark* benchmark, uint32_t path)
{
    const CheckProgram* program = &benchmark->unit.selected.program;
    const CheckProgramRows* rows = &benchmark->unit.rows;
    uint64_t kept = 0;
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const uint32_t first = rows->first[b];
        // The function's file is its first raw row's, its declaration's or the body's start's.
        const uint32_t key = rows->counts[b] != 0 ? benchmark->keys[0][first] : 0;
        uint32_t* offsets = benchmark->kept_offsets[path] + kept;
        uint32_t* lines = benchmark->kept_lines[path] + kept;
        kept += path == 0 ? check_debug_keep_scalar(rows->offsets + first, benchmark->keys[0] + first, benchmark->lines[0] + first, rows->counts[b], key, rows->bodies[b], offsets, lines) :
                            check_debug_keep(rows->offsets + first, benchmark->keys[0] + first, benchmark->lines[0] + first, rows->counts[b], key, rows->bodies[b], offsets, lines);
    }
    return kept;
}

// The whole unit by a path, its arena put back after; returns whether it was made, and its sections into `debug`.
static bool check_debug_benchmark_build(CheckDebugBenchmark* benchmark, uint32_t path, CheckDebug* debug)
{
    Arena* output = &benchmark->unit.output;
    const uint64_t mark = output->length;
    const bool made = check_debug_build(debug, &benchmark->unit.selected.program, &benchmark->unit.rows, benchmark->parser, benchmark->pp, path == 0, output,
                                        &benchmark->unit.scratch);
    arena_pop_to(output, mark);
    return made;
}

// The emission of the program by the vector path, with the raw rows (`rows`) or without, its arenas put back after.
static bool check_debug_benchmark_emit(CheckDebugBenchmark* benchmark, bool rows)
{
    CheckDebugTestUnit* unit = &benchmark->unit;
    const uint64_t output = unit->output.length;
    const uint64_t bytes = unit->bytes.length;
    CheckProgramCode code = { 0 };
    CheckProgramRows made = { 0 };
    const bool emitted = check_program_emit(&code, &unit->selected.program, benchmark->parser, benchmark->identifiers, unit->selected.data.capacity, false, &unit->output,
                                            &unit->bytes, &unit->scratch, NULL, rows ? &made : NULL, false);
    arena_pop_to(&unit->bytes, bytes);
    arena_pop_to(&unit->output, output);
    return emitted;
}

// One sample of a pass by a path (0 the exact one, 1 the vector one, and for emit 0 without rows and 1 with): its
// nanoseconds.
static uint64_t check_debug_benchmark_sample(CheckDebugBenchmark* benchmark, CheckDebugBenchmarkPass pass, uint32_t path)
{
    CheckDebug debug;
    const uint64_t start = check_source_time();
    if (pass == CHECK_DEBUG_BENCHMARK_ROWS)
    {
        check_debug_benchmark_rows(&benchmark->synthetic, path);
    }
    else if (pass == CHECK_DEBUG_BENCHMARK_LINES)
    {
        check_debug_benchmark_lines(benchmark, path);
    }
    else if (pass == CHECK_DEBUG_BENCHMARK_KEEP)
    {
        check_debug_benchmark_keep(benchmark, path);
    }
    else if (pass == CHECK_DEBUG_BENCHMARK_BUILD)
    {
        check_debug_benchmark_build(benchmark, path, &debug);
    }
    else
    {
        check_debug_benchmark_emit(benchmark, path == 1);
    }
    return check_source_time() - start;
}

// The synthetic functions of the rows pass, shaped like Tier 0's, from `tally`'s random sequence.
static bool check_debug_benchmark_synthesize(CheckDebugBenchmarkRows* rows, uint32_t functions, CheckDebugTestTally* tally)
{
    const uint64_t entries = (uint64_t)functions * BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES;
    *rows = (CheckDebugBenchmarkRows){
        .entry_records = malloc((entries + functions) * sizeof(uint32_t)),
        .tokens = malloc((entries + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t)),
        .offsets = malloc((entries * 3 + (uint64_t)functions * 8) * sizeof(uint32_t)),
        .offset_first = malloc(((uint64_t)functions + 1) * sizeof(uint32_t)),
        .lengths = malloc(((uint64_t)functions + 1) * sizeof(uint32_t)),
        .functions = functions,
    };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        rows->row_offsets[path] = malloc((entries + 2ull * functions + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
        rows->row_tokens[path] = malloc((entries + 2ull * functions + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
    }
    const bool ready = rows->entry_records != 0 && rows->tokens != 0 && rows->offsets != 0 && rows->offset_first != 0 && rows->lengths != 0 &&
                       rows->row_offsets[0] != 0 && rows->row_offsets[1] != 0 && rows->row_tokens[0] != 0 && rows->row_tokens[1] != 0;
    uint64_t offset_at = 0;
    for (uint32_t f = 0; ready && f < functions; f += 1)
    {
        uint32_t* entry_records = rows->entry_records + (uint64_t)f * (BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES + 1);
        uint32_t record = 1 + (uint32_t)(check_debug_test_random(tally) % 3);
        for (uint32_t e = 0; e < BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES; e += 1)
        {
            entry_records[e] = record;
            rows->tokens[(uint64_t)f * BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES + e] = (uint32_t)check_debug_test_random(tally);
            const uint32_t choice = (uint32_t)(check_debug_test_random(tally) % 9);
            record += choice < 3 ? 0 : 1 + choice % 3;
        }
        entry_records[BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES] = record;
        const uint32_t records = record + 2;
        rows->offset_first[f] = (uint32_t)offset_at;
        uint32_t offset = 0;
        for (uint32_t r = 0; r <= records; r += 1)
        {
            rows->offsets[offset_at + r] = offset;
            offset += 1 + (uint32_t)(check_debug_test_random(tally) % 15);
        }
        rows->lengths[f] = rows->offsets[offset_at + records];
        offset_at += records + 1;
    }
    return ready;
}

static void check_debug_benchmark_release(CheckDebugBenchmark* benchmark)
{
    CheckDebugBenchmarkRows* rows = &benchmark->synthetic;
    free(rows->entry_records);
    free(rows->tokens);
    free(rows->offsets);
    free(rows->offset_first);
    free(rows->lengths);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        free(rows->row_offsets[path]);
        free(rows->row_tokens[path]);
        free(benchmark->keys[path]);
        free(benchmark->lines[path]);
        free(benchmark->kept_offsets[path]);
        free(benchmark->kept_lines[path]);
    }
    free(benchmark->closings);
}

static int run_check_debug_benchmark(int argc, char** argv)
{
    uint32_t functions = 2000;
    uint32_t runs = 21;
    uint64_t seed = 395;
    uint32_t after_scalar = 0;
    int32_t only = -1;
    int32_t alone = -1;
    const char* print = 0;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            functions = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
        {
            runs = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            seed = strtoull(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            after_scalar = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            for (int32_t pass = 0; pass < CHECK_DEBUG_BENCHMARK_PASSES; pass += 1)
            {
                only = strcmp(argv[i], check_debug_benchmark_names[pass]) == 0 ? pass : only;
            }
            options = only >= 0;
        }
        else if (strcmp(argv[i], "--print") == 0 && i + 1 < argc)
        {
            print = argv[++i];
        }
        else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc)
        {
            i += 1;
            alone = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : -1;
            options = alone >= 0;
        }
        else
        {
            options = false;
        }
    }
    options = options && functions != 0 && runs != 0 && runs <= 1001 && after_scalar <= 1000;
    if (!options)
    {
        fprintf(stderr, "usage: compiler benchmark-check-debug [--functions N] [--seed N] [--runs N] [--only rows|lines|keep|build|emit] [--path vector|scalar] "
                        "[--after-scalar MS] [--print PATH]\n");
    }
    CheckDebugTestTally tally = { .random = (seed | 1) * 0x9e3779b97f4a7c15ull };
    if (options && print != 0)
    {
        const uint64_t capacity = (uint64_t)functions * 1024 + 4096;
        char* text = malloc(capacity);
        const uint64_t length = text != 0 ? check_debug_test_generate(&tally, text, capacity, functions, false) : capacity;
        const bool written = length < capacity && foreign_test_write(print, text, length);
        free(text);
        if (!written)
        {
            fprintf(stderr, "benchmark-check-debug: can't write %s\n", print);
        }
        options = false;
        print = written ? print : 0;
    }
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    CheckDebugBenchmark benchmark = { .pp = &pp, .parser = &parser, .identifiers = &identifiers };
    bool ready = options && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && check_debug_benchmark_synthesize(&benchmark.synthetic, functions, &tally);
    // The program: generated into a file of a directory of its own, compiled once with source rows.
    char directory[PATH_MAX];
    char path[PATH_MAX + 16];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-debug-benchmark-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = ready && mkdtemp(directory) != 0;
    ready = made;
    snprintf(path, sizeof(path), "%s/program.c", directory);
    const uint64_t capacity = (uint64_t)functions * 1024 + 4096;
    char* text = ready ? malloc(capacity) : 0;
    // A C program as most are: macros, some across lines, and no #line.
    const uint64_t length = text != 0 ? check_debug_test_generate(&tally, text, capacity, functions, false) : capacity;
    ready = ready && length < capacity && foreign_test_write(path, text, length);
    free(text);
    if (ready)
    {
        check_debug_test_compile(&benchmark.unit, &pp, &parser, &identifiers, path, directory, false);
        ready = benchmark.unit.ready;
    }
    const CheckProgramRows* rows = &benchmark.unit.rows;
    const CheckProgram* program = &benchmark.unit.selected.program;
    for (uint32_t side = 0; ready && side < 2; side += 1)
    {
        benchmark.keys[side] = malloc(((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
        benchmark.lines[side] = malloc(((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
        benchmark.kept_offsets[side] = malloc(((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
        benchmark.kept_lines[side] = malloc(((uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK) * sizeof(uint32_t));
        ready = benchmark.keys[side] != 0 && benchmark.lines[side] != 0 && benchmark.kept_offsets[side] != 0 && benchmark.kept_lines[side] != 0;
    }
    benchmark.closings = ready ? malloc(((uint64_t)program->body_count + 1) * sizeof(uint32_t)) : 0;
    ready = ready && benchmark.closings != 0 && check_debug_source(&benchmark.source, &pp, &parser, &benchmark.unit.arena);
    for (uint32_t b = 0; ready && b < program->body_count; b += 1)
    {
        benchmark.closings[b] = parser.tree.parents[program->bodies[b].opening];
    }
    // The paths must agree before anything is timed.
    bool same = ready;
    if (ready)
    {
        const uint64_t scalar_rows = check_debug_benchmark_rows(&benchmark.synthetic, 0);
        same = check_debug_benchmark_rows(&benchmark.synthetic, 1) == scalar_rows &&
               memcmp(benchmark.synthetic.row_offsets[0], benchmark.synthetic.row_offsets[1], scalar_rows * sizeof(uint32_t)) == 0 &&
               memcmp(benchmark.synthetic.row_tokens[0], benchmark.synthetic.row_tokens[1], scalar_rows * sizeof(uint32_t)) == 0;
        check_debug_benchmark_lines(&benchmark, 0);
        check_debug_benchmark_lines(&benchmark, 1);
        same = same && memcmp(benchmark.keys[0], benchmark.keys[1], (uint64_t)rows->count * sizeof(uint32_t)) == 0 &&
               memcmp(benchmark.lines[0], benchmark.lines[1], (uint64_t)rows->count * sizeof(uint32_t)) == 0;
        benchmark.kept[0] = check_debug_benchmark_keep(&benchmark, 0);
        benchmark.kept[1] = check_debug_benchmark_keep(&benchmark, 1);
        same = same && benchmark.kept[0] == benchmark.kept[1] && memcmp(benchmark.kept_offsets[0], benchmark.kept_offsets[1], benchmark.kept[0] * sizeof(uint32_t)) == 0 &&
               memcmp(benchmark.kept_lines[0], benchmark.kept_lines[1], benchmark.kept[0] * sizeof(uint32_t)) == 0;
        CheckDebug debugs[2];
        bool built = true;
        uint8_t* sections[2][BUSTER_DEBUG_SECTIONS] = { { 0 } };
        uint64_t sizes[2][BUSTER_DEBUG_SECTIONS] = { { 0 } };
        for (uint32_t side = 0; side < 2; side += 1)
        {
            // Made again without popping, so that both units' sections are there to compare.
            built &= check_debug_build(&debugs[side], program, rows, &parser, &pp, side == 0, &benchmark.unit.output, &benchmark.unit.scratch);
            for (uint32_t section = 0; built && section < BUSTER_DEBUG_SECTIONS; section += 1)
            {
                sections[side][section] = debugs[side].output.sections[section].bytes;
                sizes[side][section] = debugs[side].output.sections[section].size;
            }
        }
        for (uint32_t section = 0; built && section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            built = sizes[0][section] == sizes[1][section] && memcmp(sections[0][section], sections[1][section], sizes[0][section]) == 0;
        }
        same = same && built && check_debug_benchmark_emit(&benchmark, true) && check_debug_benchmark_emit(&benchmark, false);
        if (!same)
        {
            fprintf(stderr, "benchmark-check-debug: the vector paths don't give the exact paths' rows, lines, kept rows or sections\n");
        }
    }
    if (same)
    {
        printf("benchmark-check-debug: %u synthetic functions of %u entries; a generated program of %u functions, %u source functions, %llu typed entries, %u raw rows, "
               "%llu kept\n", functions, BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES, functions, program->body_count, (unsigned long long)benchmark.unit.code.lowered.entries,
               rows->count, (unsigned long long)benchmark.kept[0]);
        uint64_t* samples = malloc((uint64_t)runs * 2 * sizeof(uint64_t));
        for (uint32_t pass = 0; samples != 0 && pass < CHECK_DEBUG_BENCHMARK_PASSES; pass += 1)
        {
            if (only < 0 || (uint32_t)only == pass)
            {
                // A warm-up of each path, then the runs, the paths taking turns by run.
                for (uint32_t side = 0; side < 2; side += 1)
                {
                    check_debug_benchmark_sample(&benchmark, (CheckDebugBenchmarkPass)pass, side);
                }
                for (uint32_t run = 0; run < runs; run += 1)
                {
                    for (uint32_t turn = 0; turn < 2; turn += 1)
                    {
                        const uint32_t side = (run + turn) % 2;
                        if (alone < 0 || (uint32_t)alone == side)
                        {
                            const uint64_t until = side == 1 && after_scalar != 0 ? check_source_time() + (uint64_t)after_scalar * 1000000 : 0;
                            while (check_source_time() < until)
                            {
                                check_debug_benchmark_sample(&benchmark, (CheckDebugBenchmarkPass)pass, 0);
                            }
                            samples[side * runs + run] = check_debug_benchmark_sample(&benchmark, (CheckDebugBenchmarkPass)pass, side);
                        }
                    }
                }
                const double denominator = pass == CHECK_DEBUG_BENCHMARK_ROWS ? (double)functions * BUSTER_CHECK_DEBUG_BENCHMARK_ENTRIES :
                                           pass == CHECK_DEBUG_BENCHMARK_EMIT ? (double)benchmark.unit.code.lowered.entries : (double)rows->count;
                const char* unit = pass == CHECK_DEBUG_BENCHMARK_ROWS || pass == CHECK_DEBUG_BENCHMARK_EMIT ? "entry" : "raw row";
                const double scalar = alone == 1 ? 0 : (double)check_source_median(samples, runs);
                const double vector = alone == 0 ? 0 : (double)check_source_median(samples + runs, runs);
                if (pass == CHECK_DEBUG_BENCHMARK_EMIT)
                {
                    printf("  emit: %.3f ms without the raw rows, %.3f ms with (%.2f ns per %s with, %+.1f%%)\n", scalar / 1e6, vector / 1e6, vector / denominator, unit,
                           scalar != 0 ? (vector - scalar) * 100 / scalar : 0);
                }
                else
                {
                    printf("  %s: vector %.3f ns per %s (%.3f ms), exact %.3f ns per %s (%.3f ms)%s\n", check_debug_benchmark_names[pass], vector / denominator, unit,
                           vector / 1e6, scalar / denominator, unit, scalar / 1e6, after_scalar != 0 ? ", each vector sample after the exact path" : "");
                }
            }
        }
        free(samples);
    }
    check_debug_test_close(&benchmark.unit);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    check_debug_benchmark_release(&benchmark);
    if (made)
    {
        char* remove[] = { (char*)"rm", (char*)"-rf", directory, 0 };
        object_test_run(remove, "/dev/null", "/dev/null");
    }
    return same || print != 0 ? 0 : 1;
}
