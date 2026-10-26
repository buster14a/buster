// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-backend-source's outcomes. Original fixtures: files whose functions join the corpus (the referenced ones,
// as -c lowers them, from one file and from two), one the checker rejects, which is left out, one that doesn't
// preprocess, which fails the command, and a run left with no function, which fails it too. The backend's refusals by
// name are injected, as the lowering benchmark's tests inject them: which constructs it refuses changes as it grows.
// Each case's corpus, counts, exit status and report lines are checked. Include after tier0_source_benchmark.c and
// c_lowering_source_benchmark_test.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_TIER0_SOURCE_BENCHMARK_TEST_INCLUDED
#define BUSTER_TIER0_SOURCE_BENCHMARK_TEST_INCLUDED

typedef struct BackendSourceTestCase BackendSourceTestCase;
struct BackendSourceTestCase
{
    const char* label;
    // The fixtures it measures, in order, as indices into the fixtures, ended by -1.
    int8_t files[3];
    // The 1-based index among them of the file whose emission is refused by name, or 0.
    uint32_t refuse;
    uint32_t functions;
    uint64_t measured;
    uint64_t left_out;
    uint64_t failures;
    int status;
    // What its report must hold, each once: null where it has fewer.
    const char* report[4];
};

// Runs the benchmark on `count` of `paths` with `refuse`, its report captured: whether it ran, with the corpus and
// counts in `source`, the exit status, and the report (malloc'd, the caller's to free).
static bool backend_source_test_run(char** paths, uint32_t count, uint32_t refuse, BackendSource* source, int* status, char** report)
{
    FILE* capture = tmpfile();
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    bool ran = capture != 0 && saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    char* options[] = { "-nostdinc" };
    *source = (BackendSource){ 0 };
    const bool valid = ran && backend_source_files(source, paths, count, options, BUSTER_ARRAY_LENGTH(options), BUSTER_TIER0_BENCHMARK_VARIANTS, 0, refuse);
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
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const BackendSourceTestCase cases[] = {
        {
            .label = "a file's referenced functions are measured",
            .files = { 0, -1 },
            .functions = 2,
            .measured = 1,
            .report = { "/0.c functions=2 entries=", "benchmark-backend-source: 2 functions, ", " totals files=1 left_out=0 failures=0\n", "\nencode " },
        },
        {
            .label = "two files' functions make one corpus",
            .files = { 0, 3, -1 },
            .functions = 4,
            .measured = 2,
            .report = { "/0.c functions=2 entries=", "/3.c functions=2 entries=", "benchmark-backend-source: 4 functions, ", " totals files=2 left_out=0 failures=0\n" },
        },
        {
            .label = "the checker's rejection leaves the file out",
            .files = { 1, 0, -1 },
            .functions = 2,
            .measured = 1,
            .left_out = 1,
            .report = { "/1.c rejected=\"", "use of undeclared identifier\"\n", " totals files=1 left_out=1 failures=0\n" },
        },
        {
            .label = "a file that doesn't preprocess fails the command",
            .files = { 2, 0, -1 },
            .functions = 2,
            .measured = 1,
            .failures = 1,
            .status = 1,
            .report = { "/2.c failed=\"it couldn't be preprocessed\"\n", " totals files=1 left_out=0 failures=1\n" },
        },
        {
            .label = "a refusal by name leaves the file out",
            .files = { 0, 3, -1 },
            .refuse = 1,
            .functions = 2,
            .measured = 1,
            .left_out = 1,
            .report = { "/0.c unsupported=\"the tests' injection\"\n", "/3.c functions=2 entries=", " totals files=1 left_out=1 failures=0\n" },
        },
        {
            .label = "a run with no function fails the command",
            .files = { 1, -1 },
            .left_out = 1,
            .status = 1,
            .report = { "backend-source: no function was measured\n", " totals files=0 left_out=1 failures=0\n" },
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
        const bool ran = backend_source_test_run(files, count, item->refuse, &source, &status, &report);
        bool reported = ran;
        for (uint32_t i = 0; reported && i < BUSTER_ARRAY_LENGTH(item->report) && item->report[i] != 0; i += 1)
        {
            const char* found = strstr(report, item->report[i]);
            reported = found != 0 && strstr(found + 1, item->report[i]) == 0;
        }
        // A corpus of functions has their entries and records.
        const bool corpus = source.corpus.count == item->functions && (source.corpus.count == 0 || (source.corpus.entries != 0 && source.corpus.records != 0));
        const bool expected = ran && corpus && source.files == item->measured && source.left_out == item->left_out && source.failures == item->failures &&
                              status == item->status && reported;
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
    for (uint32_t i = 0; i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
