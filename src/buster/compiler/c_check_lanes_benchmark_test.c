// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-check-lanes' preparation (#1045): the program it emits is the one -c compiles for the target its options give.
// Original fixtures run through the command on one lane and one pass: a unit of 512-bit vectors, which the baseline x86-64
// profile refuses ("vector results wider than the target's registers") and znver5 takes, must be measured with
// -march=znver5 and fail at its emission for the baseline, as -c does; and a baseline unit whose function's target
// attribute ("avx2") takes a 32-byte vector must be measured, which it is only when the program has the options'
// predefines to resolve the attribute against. Each case's exit status and report line are checked.
// Include after c_check_lanes_benchmark.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_CHECK_LANES_BENCHMARK_TEST_INCLUDED
#define BUSTER_CHECK_LANES_BENCHMARK_TEST_INCLUDED

// The command on `arguments`, its standard output captured and its errors to /dev/null: whether it ran, with its exit
// status and its report (malloc'd, the caller's to free).
static bool check_lanes_benchmark_test_run(char** arguments, uint32_t count, int* status, char** report)
{
    FILE* capture = tmpfile();
    fflush(stdout);
    fflush(stderr);
    const int saved[2] = { dup(STDOUT_FILENO), dup(STDERR_FILENO) };
    const int null = open("/dev/null", O_WRONLY);
    bool ran = capture != 0 && saved[0] >= 0 && saved[1] >= 0 && null >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(null, STDERR_FILENO) >= 0;
    *status = ran ? run_check_lanes_benchmark((int)count, arguments) : -1;
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

static __attribute__((noinline)) void check_test_check_lanes_benchmark(CheckTest* test)
{
    static const char* const fixtures[] = {
        // Two functions that return and add 512-bit vectors, which -c lowers for znver5 and refuses for the baseline.
        "typedef int V __attribute__((vector_size(64)));\n"
        "V add(V a, V b)\n{\n    return a + b;\n}\n"
        "int first(V a)\n{\n    V c = add(a, a);\n    return c[0];\n}\n",
        // A 32-byte vector in a function whose target attribute adds AVX2 to the baseline: the unit's target refuses it.
        "typedef int V8 __attribute__((vector_size(32)));\n"
        "__attribute__((target(\"avx2\"))) static int wide(int x)\n{\n    V8 a = { 0 };\n    a[0] = x;\n    V8 b = a + a;\n    return b[0];\n}\n"
        "int main(void)\n{\n    return wide(3) - 6;\n}\n",
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const char* const names[FIXTURES] = { "vectors.c", "attribute.c" };
    static const struct
    {
        const char* label;
        uint32_t fixture;
        const char* march;
        int status;
        // What its report must hold, once.
        const char* report;
    } cases[] = {
        {
            .label = "check-lanes: a unit of 512-bit vectors is measured for -march=znver5",
            .fixture = 0,
            .march = "-march=znver5",
            .status = 0,
            .report = "/vectors.c functions=2 ",
        },
        {
            .label = "check-lanes: a unit of 512-bit vectors fails for the baseline profile, as -c refuses it",
            .fixture = 0,
            .march = "-march=x86-64",
            .status = 1,
            .report = "/vectors.c failed=\"it couldn't be emitted on one lane, or its object built\"\n",
        },
        {
            .label = "check-lanes: a baseline unit's function with a target attribute takes its attribute's vectors",
            .fixture = 1,
            .march = "-march=x86-64",
            .status = 0,
            .report = "/attribute.c functions=2 ",
        },
    };
    char directory[] = "/tmp/buster-check-lanes-XXXXXX";
    char paths[FIXTURES][256];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", directory, names[i]);
        ready = parse_test_write(paths[i], fixtures[i], strlen(fixtures[i]));
    }
    check_test_expect(test, ready, "check-lanes benchmark fixtures are written");
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* arguments[] = { "--passes", "1", "--lanes", "1", "-nostdinc", (char*)cases[c].march, paths[cases[c].fixture] };
        int status = 0;
        char* report = 0;
        const bool ran = check_lanes_benchmark_test_run(arguments, BUSTER_ARRAY_LENGTH(arguments), &status, &report);
        const char* found = ran ? strstr(report, cases[c].report) : 0;
        const bool expected = ran && status == cases[c].status && found != 0 && strstr(found + 1, cases[c].report) == 0;
        check_test_expect(test, expected, cases[c].label);
        if (!expected)
        {
            printf("check-lanes case \"%s\": status=%d (%d) report:\n%s", cases[c].label, status, cases[c].status, report != 0 ? report : "(none)\n");
        }
        free(report);
    }
    for (uint32_t i = 0; i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", directory, names[i]);
        unlink(paths[i]);
    }
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
