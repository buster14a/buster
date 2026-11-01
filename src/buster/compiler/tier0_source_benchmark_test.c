// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-backend-source's outcomes. Original fixtures: files whose functions join the corpus (the referenced ones,
// as -c lowers them, from one file and from two), one the checker rejects, which is left out, one that doesn't
// preprocess, which fails the command, and a run left with no function, which fails it too. The backend's refusals by
// name are injected, as the lowering benchmark's tests inject them: which constructs it refuses changes as it grows. So
// are resources that run out (#514's review): the arena the corpus's records are compiled into, the timed runs'
// scratch, and the corpus's room for functions, full at the second of a file's two and not at its last; and a file
// whose one function lowers to no typed entry. Then the options' target (#1041): a unit of <immintrin.h>'s 512-bit
// intrinsics, which -march=znver5 compiles (its masks Tier 0's opmask class, #179) and the baseline profile refuses by
// name, leaving it out; and, without -march, a function whose target("bmi2") attribute its BMI2 builtin needs, measured
// as -c compiles it for its attribute's target (#1041's review), where the unit's would refuse the builtin by name.
// Each case's corpus, counts, exit status and report lines are checked, the totals last with the measurement's
// outcome. Then the command's repeated --only, and tier0_benchmark_add at its corpus's capacity, which must write
// nothing. Include after tier0_source_benchmark.c and c_lowering_source_benchmark_test.c; test-checker calls this
// through its forward declaration.
#ifndef BUSTER_TIER0_SOURCE_BENCHMARK_TEST_INCLUDED
#define BUSTER_TIER0_SOURCE_BENCHMARK_TEST_INCLUDED

typedef struct BackendSourceTestCase BackendSourceTestCase;
struct BackendSourceTestCase
{
    const char* label;
    // The fixtures it measures, in order, as indices into the fixtures, ended by -1.
    int8_t files[3];
    // The failures injected (BackendSourceFault): a refusal by name, a full corpus, arenas out of room.
    BackendSourceFault fault;
    uint32_t functions;
    uint64_t measured;
    uint64_t left_out;
    uint64_t failures;
    // Whether its corpus's functions lower to no typed entry (fixture 4's).
    bool no_entries;
    // Whether it reads the standard headers (without -nostdinc), and its -march option (null for none: the baseline).
    bool includes;
    const char* march;
    int status;
    // What its report must hold, each once: null where it has fewer.
    const char* report[4];
};

// Runs the benchmark on `count` of `paths` with `fault` and the case's options (`includes` and `march`), its report
// captured: whether it ran, with the corpus and counts in `source`, the exit status, and the report (malloc'd, the
// caller's to free).
static bool backend_source_test_run(char** paths, uint32_t count, BackendSourceFault fault, bool includes, const char* march, BackendSource* source,
                                    int* status, char** report)
{
    FILE* capture = tmpfile();
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    bool ran = capture != 0 && saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    char* options[2];
    uint32_t option_count = 0;
    if (!includes)
    {
        options[option_count++] = "-nostdinc";
    }
    if (march != 0)
    {
        options[option_count++] = (char*)march;
    }
    *source = (BackendSource){ 0 };
    const bool valid = ran && backend_source_files(source, paths, count, options, option_count, BUSTER_TIER0_BENCHMARK_VARIANTS, 0, fault);
    *status = backend_source_status(valid, source);
    fflush(stdout);
    fflush(stderr);
    ran = saved[0] >= 0 && dup2(saved[0], STDOUT_FILENO) >= 0 && ran;
    ran = saved[1] >= 0 && dup2(saved[1], STDERR_FILENO) >= 0 && ran;
    const long length = ran && fseek(capture, 0, SEEK_END) == 0 ? ftell(capture) : -1;
    char* text = length >= 0 && length < (1l << 24) && fseek(capture, 0, SEEK_SET) == 0 ? calloc((size_t)length + 1, 1) : 0;
    ran = text != 0 && fread(text, 1, (size_t)length, capture) == (size_t)length;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (saved[i] >= 0)
        {
            close(saved[i]);
        }
    }
    if (null >= 0)
    {
        close(null);
    }
    if (capture != 0)
    {
        fclose(capture);
    }
    *report = text;
    return ran;
}

// The command itself on `arguments`, its output to /dev/null: its exit status, or -1 if the output couldn't be moved.
static int backend_source_test_command(char** arguments, uint32_t count)
{
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    const bool moved = saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(null, STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int status = moved ? run_backend_source_benchmark((int)count, arguments) : -1;
    fflush(stdout);
    fflush(stderr);
    bool restored = saved[0] >= 0 && dup2(saved[0], STDOUT_FILENO) >= 0;
    restored = saved[1] >= 0 && dup2(saved[1], STDERR_FILENO) >= 0 && restored;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (saved[i] >= 0)
        {
            close(saved[i]);
        }
    }
    if (null >= 0)
    {
        close(null);
    }
    return restored ? status : -1;
}

// tier0_benchmark_add at its corpus's capacity (#514's review): a corpus with room for one function takes the first of a
// generated program's and refuses the second as memory, writing nothing. An entry past `sized`, `offsets` or `ends`
// would be the next array's first, which must still hold the first function's.
static void check_test_backend_corpus_capacity(CheckTest* test)
{
    Tier0Tables tables;
    Arena arena = arena_reserve(UINT64_C(1) << 30);
    Arena code_arena = arena_reserve(UINT64_C(1) << 30);
    const Tier0ProgramOptions options = {
        .functions = 2,
        .statements = 6,
        .expression_nodes = 6,
        .calls = true,
        .loops = true,
        .floating = false,
        .pointers = false,
        .defined = false,
    };
    Tier0Program program;
    Tier0Functions functions;
    Tier0BenchmarkCorpus corpus;
    const bool opened = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_tables_open(&tables);
    const bool ready = opened && arena.base != 0 && code_arena.base != 0 && tier0_generate_program(&program, &arena, 1, options, "c_") &&
                       tier0_program_functions(&program, &tables, &arena, &functions) && functions.count >= 2 && tier0_benchmark_reserve(&corpus, &arena, 1);
    check_test_expect(test, ready, "backend corpus capacity: two generated functions and a corpus with room for one");
    if (ready)
    {
        const Tier0BenchmarkAdd first = tier0_benchmark_add(&corpus, &functions.functions[0], &arena, &code_arena);
        const Tier0Code code = corpus.codes[0];
        uint32_t* const offsets = corpus.offsets[0];
        uint32_t* const ends = corpus.ends[0];
        uint8_t* const machine = corpus.machine[0];
        const Tier0BenchmarkAdd second = tier0_benchmark_add(&corpus, &functions.functions[1], &arena, &code_arena);
        check_test_expect(test, first == TIER0_BENCHMARK_ADDED && second == TIER0_BENCHMARK_MEMORY && corpus.count == 1,
                          "backend corpus capacity: the first function joins and the second finds the corpus full");
        check_test_expect(test, corpus.offsets[0] == offsets && corpus.ends[0] == ends && corpus.machine[0] == machine && corpus.codes[0].count == code.count &&
                                corpus.functions[0] == &functions.functions[0],
                          "backend corpus capacity: a full corpus's add writes nothing");
    }
    if (opened)
    {
        tier0_tables_close(&tables);
    }
    arena_release(&code_arena);
    arena_release(&arena);
}

static __attribute__((noinline)) void check_test_backend_source_benchmark(CheckTest* test)
{
    static const char* const fixtures[] = {
        // Two functions -c lowers, f and main; nothing refers to unused.
        "static int unused(void) { return 2; }\nint f(int x) { return x + 1; }\nint main(void) { return f(1) - 2; }\n",
        // The checker rejects it.
        "int main(void) { return undeclared; }\n",
        // It doesn't preprocess.
        "#include \"missing.h\"\nint main(void) { return 0; }\n",
        // Loops, branches, calls and floating values, for sizing and encoding.
        "double g(double x, int n)\n{\n    double s = 0;\n    for (int i = 0; i < n; i += 1)\n    {\n        s += x * i;\n        if (s > 100)\n        {\n"
        "            break;\n        }\n    }\n    return s;\n}\nint h(int* p, int n)\n{\n    int t = 0;\n    while (n-- > 0)\n    {\n        t += p[n] > 0 ? p[n] : -p[n];\n"
        "    }\n    return t + (int)g(1.5, n);\n}\n",
        // Its one function lowers to no typed entry (#514's review): falling off an int function's end returns nothing,
        // and Tier 0 makes only its prologue and epilogue.
        "int f(void) {}\n",
        // 512-bit vectors and their masks, through <immintrin.h>'s intrinsics (#1041): what -march=znver5 compiles, its
        // masks in Tier 0's opmask class (#179), and the baseline profile refuses by name.
        "#include <immintrin.h>\n__m512i mix(__m512i a, __m512i b)\n{\n    const __mmask16 k = _mm512_cmpgt_epi32_mask(a, b);\n"
        "    const __mmask16 m = _mm512_kand(k, _mm512_cmplt_epi32_mask(b, _mm512_set1_epi32(8)));\n    return _mm512_mask_add_epi32(a, m, a, b);\n}\n"
        "int main(void)\n{\n    const __m512i c = mix(_mm512_set1_epi32(3), _mm512_set1_epi32(1));\n    return _mm512_cvtsi512_si32(c) - 4;\n}\n",
        // A function whose target attribute its builtin needs (#1041's review): without -march the unit's target is the
        // baseline's, which hasn't BMI2.
        "__attribute__((target(\"bmi2\"))) unsigned deposit(unsigned x, unsigned m)\n{\n    return __builtin_ia32_pdep_si(x, m);\n}\n"
        "int main(void)\n{\n    return (int)deposit(5u, 0xf0u) - 0x50;\n}\n",
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const BackendSourceTestCase cases[] = {
        {
            .label = "a file's referenced functions are measured",
            .files = { 0, -1 },
            .functions = 2,
            .measured = 1,
            .report = { "/0.c functions=2 entries=", "benchmark-backend-source: 2 functions, ",
                        " totals files=1 left_out=0 failures=0 measurement=reported\n", "\nencode " },
        },
        {
            .label = "two files' functions make one corpus",
            .files = { 0, 3, -1 },
            .functions = 4,
            .measured = 2,
            .report = { "/0.c functions=2 entries=", "/3.c functions=2 entries=", "benchmark-backend-source: 4 functions, ",
                        " totals files=2 left_out=0 failures=0 measurement=reported\n" },
        },
        {
            .label = "the checker's rejection leaves the file out",
            .files = { 1, 0, -1 },
            .functions = 2,
            .measured = 1,
            .left_out = 1,
            .report = { "/1.c rejected=\"", "use of undeclared identifier\"\n", " totals files=1 left_out=1 failures=0 measurement=reported\n" },
        },
        {
            .label = "a file that doesn't preprocess fails the command",
            .files = { 2, 0, -1 },
            .functions = 2,
            .measured = 1,
            .failures = 1,
            .status = 1,
            .report = { "/2.c failed=\"it couldn't be preprocessed\"\n", " totals files=1 left_out=0 failures=1 measurement=reported\n" },
        },
        {
            .label = "a refusal by name leaves the file out",
            .files = { 0, 3, -1 },
            .fault = { .refuse = 1 },
            .functions = 2,
            .measured = 1,
            .left_out = 1,
            .report = { "/0.c unsupported=\"the tests' injection\"\n", "/3.c functions=2 entries=",
                        " totals files=1 left_out=1 failures=0 measurement=reported\n" },
        },
        {
            .label = "a run with no function fails the command",
            .files = { 1, -1 },
            .left_out = 1,
            .status = 1,
            .report = { "backend-source: no function was measured\n", " totals files=0 left_out=1 failures=0 measurement=none\n" },
        },
        {
            .label = "a compile that runs out of memory fails the command, its file left out",
            .files = { 0, 3, -1 },
            .fault = { .code_bytes = 64 },
            .failures = 2,
            .status = 1,
            .report = { "/0.c failed=\"memory ran out\" function=0 of 2\n", "/3.c failed=\"memory ran out\" function=0 of 2\n",
                        " totals files=0 left_out=0 failures=2 measurement=none\n" },
        },
        {
            .label = "a corpus full at a file's second function fails the command",
            .files = { 0, -1 },
            .fault = { .capacity = 1 },
            .failures = 1,
            .status = 1,
            .report = { "/0.c failed=\"memory ran out\" function=1 of 2\n", " totals files=0 left_out=0 failures=1 measurement=none\n" },
        },
        {
            .label = "a corpus full with a file's last function measures it",
            .files = { 0, -1 },
            .fault = { .capacity = 2 },
            .functions = 2,
            .measured = 1,
            .report = { "/0.c functions=2 entries=", "benchmark-backend-source: 2 functions, ",
                        " totals files=1 left_out=0 failures=0 measurement=reported\n" },
        },
        {
            .label = "a timed run that runs out of memory fails the command, with no report",
            .files = { 0, -1 },
            .fault = { .scratch_bytes = 64 },
            .functions = 2,
            .measured = 1,
            .status = 1,
            .report = { "benchmark-backend-source: a function's vector path's compile failed, so its runs measured less than the corpus: no report\n",
                        " totals files=1 left_out=0 failures=0 measurement=failed\n" },
        },
        {
            .label = "a corpus of functions without a typed entry measures nothing, and fails the command",
            .files = { 4, -1 },
            .functions = 1,
            .measured = 1,
            .no_entries = true,
            .status = 1,
            .report = { "/4.c functions=1 entries=0 records=",
                        "benchmark-backend-source: no typed entry in the corpus's functions (1), so nothing to measure per entry: no report\n",
                        " totals files=1 left_out=0 failures=0 measurement=none\n" },
        },
        {
            .label = "a unit of 512-bit intrinsics is measured for the options' -march=znver5",
            .files = { 5, -1 },
            .includes = true,
            .march = "-march=znver5",
            // mix, main and the five functions of the header they reach (its compares are macros).
            .functions = 7,
            .measured = 1,
            .report = { "/5.c functions=7 entries=", "benchmark-backend-source: 7 functions, ",
                        " totals files=1 left_out=0 failures=0 measurement=reported\n" },
        },
        {
            .label = "a unit of 512-bit intrinsics is left out for the baseline profile, without -march",
            .files = { 5, 0, -1 },
            .includes = true,
            .functions = 2,
            .measured = 1,
            .left_out = 1,
            .report = { "/5.c unsupported=\"vector results wider than the target's registers\"\n", "/0.c functions=2 entries=",
                        " totals files=1 left_out=1 failures=0 measurement=reported\n" },
        },
        {
            .label = "a function's target(\"bmi2\") attribute gives its builtin's instruction without -march",
            .files = { 6, -1 },
            .functions = 2,
            .measured = 1,
            .report = { "/6.c functions=2 entries=", "benchmark-backend-source: 2 functions, ",
                        " totals files=1 left_out=0 failures=0 measurement=reported\n" },
        },
    };
    char directory[] = "/tmp/buster-backend-source-XXXXXX";
    char paths[FIXTURES][256];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i], strlen(fixtures[i]));
    }
    check_test_expect(test, ready, "backend-source benchmark fixtures are written");
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const BackendSourceTestCase* item = cases + c;
        char* files[BUSTER_ARRAY_LENGTH(item->files)];
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(item->files) && item->files[count] >= 0)
        {
            files[count] = paths[item->files[count]];
            count += 1;
        }
        BackendSource source;
        int status = 0;
        char* report = 0;
        const bool ran = backend_source_test_run(files, count, item->fault, item->includes, item->march, &source, &status, &report);
        bool reported = ran;
        for (uint32_t i = 0; reported && i < BUSTER_ARRAY_LENGTH(item->report) && item->report[i] != 0; i += 1)
        {
            const char* found = strstr(report, item->report[i]);
            reported = found != 0 && strstr(found + 1, item->report[i]) == 0;
        }
        // The totals come last, after the measurement's report or what failed it (#514's review).
        const char* totals = report != 0 ? strstr(report, "backend-source: totals ") : 0;
        const bool last = totals != 0 && strchr(totals, '\n') == report + strlen(report) - 1;
        // A corpus of functions has their records, and their entries but where the case says they lower to none.
        const bool corpus = source.corpus.count == item->functions &&
                            (source.corpus.count == 0 || (source.corpus.records != 0 && (source.corpus.entries == 0) == item->no_entries));
        const bool expected = ran && corpus && source.files == item->measured && source.left_out == item->left_out && source.failures == item->failures &&
                              status == item->status && reported && last;
        check_test_expect(test, expected, item->label);
        if (!expected)
        {
            printf("backend-source case \"%s\": functions=%u (%u) files=%" PRIu64 " (%" PRIu64 ") left_out=%" PRIu64 " (%" PRIu64 ") failures=%" PRIu64 " (%" PRIu64
                   ") status=%d (%d) report:\n%s",
                   item->label, source.corpus.count, item->functions, source.files, item->measured, source.left_out, item->left_out, source.failures,
                   item->failures, status, item->status, report != 0 ? report : "(none)\n");
        }
        free(report);
    }
    // The command's --only (#514's review): each names its variant or is refused, whatever one before it named; of two
    // that name variants, the last is measured.
    static const struct
    {
        const char* first;
        const char* second;
        int status;
    } onlies[] = {
        { "vector", "typo", 1 },
        { "typo", "vector", 1 },
        { "scalar", "encode", 0 },
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(onlies); i += 1)
    {
        char* arguments[] = { "--only", (char*)onlies[i].first, "--only", (char*)onlies[i].second, "-nostdinc", paths[0] };
        const int status = backend_source_test_command(arguments, BUSTER_ARRAY_LENGTH(arguments));
        char description[160];
        snprintf(description, sizeof(description), "backend-source: --only %s --only %s exits with %d", onlies[i].first, onlies[i].second, onlies[i].status);
        check_test_expect(test, status == onlies[i].status, description);
    }
    for (uint32_t i = 0; i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (ready)
    {
        rmdir(directory);
    }
    check_test_backend_corpus_capacity(test);
}

#endif
