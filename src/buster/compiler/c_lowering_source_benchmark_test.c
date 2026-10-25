// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-lowering-source's outcomes (#418's review). Original fixtures: a file that lowers both ways is measured,
// one the checker rejects is left out, and one that doesn't preprocess fails the command. Then failures injected where
// real ones arise (the checker, the checked program's preparation, a warm-up's emission and its static data's
// publication, the referenced way's emission in the last timed run), each with the status of a refusal by name and of
// a resource's failure (the checker's only the latter): the preparation's, the emission's and the publication's
// refusals leave their file out, and every other fails the command. So does a run that measures no file. The backend's
// own refusals are injected too, by the status they have: which constructs it refuses changes as it grows (main's long
// double once made a refused fixture lower). Each case's counts, exit status and report lines are checked. Include
// after c_lowering_source_benchmark.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_LOWERING_SOURCE_BENCHMARK_TEST_INCLUDED
#define BUSTER_C_LOWERING_SOURCE_BENCHMARK_TEST_INCLUDED

typedef struct LoweringSourceTestCase LoweringSourceTestCase;
struct LoweringSourceTestCase
{
    const char* label;
    // The fixtures it measures, in order, as indices into the fixtures, ended by -1.
    int8_t files[3];
    LoweringSourceFault fault;
    uint64_t measured;
    uint64_t failures;
    int status;
    // What its report must hold, each at most once: null where it has fewer.
    const char* report[3];
};

// Runs the benchmark on `count` of `paths` with `fault`, its report captured: whether it ran, with its totals, exit
// status and report (malloc'd, the caller's to free) in the results.
static bool lowering_source_test_run(char** paths, uint32_t count, LoweringSourceFault fault, LoweringSourceTotals* totals, int* status, char** report)
{
    FILE* capture = tmpfile();
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    bool ran = capture != 0 && saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    char* options[] = { "-nostdinc" };
    *totals = (LoweringSourceTotals){ 0 };
    const bool valid = ran && lowering_source_files(totals, paths, count, options, BUSTER_ARRAY_LENGTH(options), 3, fault);
    *status = lowering_source_status(valid, totals);
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

static __attribute__((noinline)) void check_test_lowering_source_benchmark(CheckTest* test)
{
    static const char* const fixtures[] = {
        // Lowers both ways: the referenced definitions are f and main, and everything is unused too.
        "static int unused(void) { return 2; }\nint f(int x) { return x + 1; }\nint main(void) { return f(1) - 2; }\n",
        // The checker rejects it.
        "int main(void) { return undeclared; }\n",
        // It doesn't preprocess.
        "#include \"missing.h\"\nint main(void) { return 0; }\n",
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const LoweringSourceTestCase cases[] = {
        {
            .label = "a file that lowers both ways",
            .files = { 0, -1 },
            .measured = 1,
            .report = { "/0.c functions_defined=3 functions_lowered=2 functions_skipped=1 ", " totals files=1 ", " failures=0\n" },
        },
        {
            .label = "the checker's rejection",
            .files = { 1, 0, -1 },
            .measured = 1,
            .report = { "/1.c rejected=\"", "use of undeclared identifier\"\n", " failures=0\n" },
        },
        {
            .label = "a file that doesn't preprocess",
            .files = { 2, 0, -1 },
            .measured = 1,
            .failures = 1,
            .status = 1,
            .report = { "/2.c failed=\"it couldn't be preprocessed\"\n", " totals files=1 ", " failures=1\n" },
        },
        {
            .label = "a refusal by name of lowering everything",
            .files = { 0, 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_EMIT, .unsupported = true, .ways = 2, .file = 1 },
            .measured = 1,
            .report = { " referenced=\"lowered\" all=\"unsupported: the tests' injection\"\n", " totals files=1 ", " failures=0\n" },
        },
        {
            .label = "a refusal by name of both ways",
            .files = { 0, 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_EMIT, .unsupported = true, .ways = 3, .file = 1 },
            .measured = 1,
            .report = { " referenced=\"unsupported: the tests' injection\" all=\"unsupported: the tests' injection\"\n", " totals files=1 " },
        },
        {
            .label = "a run that measures no file",
            .files = { 1, 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_EMIT, .unsupported = true, .ways = 2, .file = 2 },
            .status = 1,
            .report = { "lowering-source: no file was measured both ways\n", " totals files=0 ", " failures=0\n" },
        },
        {
            .label = "the checker's running out of a resource",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_CHECK },
            .failures = 1,
            .status = 1,
            .report = { "/0.c failed=\"the checker ran out of a resource\"\n", " failures=1\n" },
        },
        {
            .label = "the preparation's refusal by name",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_PREPARE, .unsupported = true },
            .status = 1,
            .report = { "/0.c unsupported=\"its checked program: an unsupported C statement\"\n", " failures=0\n" },
        },
        {
            .label = "the preparation's failure",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_PREPARE },
            .failures = 1,
            .status = 1,
            .report = { "/0.c failed=\"its static data, checked program or arenas couldn't be prepared\"\n", " failures=1\n" },
        },
        {
            .label = "the warm-up emission's refusal by name",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_EMIT, .unsupported = true, .ways = 1 },
            .status = 1,
            .report = { " referenced=\"unsupported: the tests' injection\" all=\"lowered\"\n", " failures=0\n" },
        },
        {
            .label = "the warm-up emission's failure",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_EMIT, .ways = 1 },
            .failures = 1,
            .status = 1,
            .report = { " referenced=\"the backend failed: the tests' injection\" all=\"lowered\" failed=\"", " failures=1\n" },
        },
        {
            .label = "the warm-up publication's refusal by name",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_PUBLISH, .unsupported = true, .ways = 1 },
            .status = 1,
            .report = { " referenced=\"unsupported: its static data's publication\" all=\"lowered\"\n", " failures=0\n" },
        },
        {
            .label = "the warm-up publication's failure",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_PUBLISH, .ways = 1 },
            .failures = 1,
            .status = 1,
            .report = { " referenced=\"its static data couldn't be published\" all=\"lowered\" failed=\"", " failures=1\n" },
        },
        {
            .label = "a timed run's refusal by name",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_TIMED, .unsupported = true },
            .failures = 1,
            .status = 1,
            .report = { " failed=\"timed pass 3 of referenced: unsupported: the tests' injection\"\n", " failures=1\n" },
        },
        {
            .label = "a timed run's failure",
            .files = { 0, -1 },
            .fault = { .stage = LOWERING_SOURCE_STAGE_TIMED },
            .failures = 1,
            .status = 1,
            .report = { " failed=\"timed pass 3 of referenced: the backend failed: the tests' injection\"\n", " failures=1\n" },
        },
    };
    char directory[] = "/tmp/buster-lowering-source-XXXXXX";
    char paths[FIXTURES][256];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i], strlen(fixtures[i]));
    }
    check_test_expect(test, ready, "lowering-source benchmark fixtures are written");
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const LoweringSourceTestCase* item = cases + c;
        char* files[BUSTER_ARRAY_LENGTH(item->files)];
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(item->files) && item->files[count] >= 0)
        {
            files[count] = paths[item->files[count]];
            count += 1;
        }
        LoweringSourceTotals totals;
        int status = 0;
        char* report = 0;
        const bool ran = lowering_source_test_run(files, count, item->fault, &totals, &status, &report);
        bool reported = ran;
        for (uint32_t i = 0; reported && i < BUSTER_ARRAY_LENGTH(item->report) && item->report[i] != 0; i += 1)
        {
            const char* found = strstr(report, item->report[i]);
            reported = found != 0 && strstr(found + 1, item->report[i]) == 0;
        }
        const bool expected = ran && totals.files == item->measured && totals.failures == item->failures && status == item->status && reported;
        check_test_expect(test, expected, item->label);
        if (!expected)
        {
            printf("lowering-source case \"%s\": files=%" PRIu64 " (%" PRIu64 ") failures=%" PRIu64 " (%" PRIu64 ") status=%d (%d) report:\n%s", item->label,
                   totals.files, item->measured, totals.failures, item->failures, status, item->status, report != 0 ? report : "(none)\n");
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
