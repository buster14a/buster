// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-check-lanes' preparation (#1045): the program it emits is the one -c compiles for the target its options give.
// Original fixtures run through the command on one lane and one pass: a unit of 512-bit vectors, which the baseline x86-64
// profile refuses (their qwords' multiplication: "vector multiplication of 64-bit elements without AVX-512DQ"; their
// variable shifts were #2239's) and znver5 takes, must be measured with -march=znver5 and fail at its emission for the baseline, as -c does; and a baseline unit
// whose function's target
// attribute ("avx2") takes a 32-byte vector must be measured, which it is only when the program has the options'
// predefines to resolve the attribute against. Then #1057's review: a unit whose always_inline callee's target -c's
// target-feature check refuses is left out with its configuration's receipt, and the file after it measured; alone, it
// leaves the run with no file measured, which fails; and a refused request (-fshort-enums) fails with the driver's
// refusal. Each case's exit status and report lines are checked.
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
        // Two functions that return and multiply 512-bit vectors of qwords, which -c lowers for znver5 (AVX-512DQ's
        // VPMULLQ) and refuses for the baseline (#1834's part B has its qword multiplication).
        "typedef long long V __attribute__((vector_size(64)));\n"
        "V add(V a, V b)\n{\n    return a * b;\n}\n"
        "int first(V a)\n{\n    V c = add(a, a);\n    return c[0];\n}\n",
        // A 32-byte vector in a function whose target attribute adds AVX2 to the baseline: the unit's target refuses it.
        "typedef int V8 __attribute__((vector_size(32)));\n"
        "__attribute__((target(\"avx2\"))) static int wide(int x)\n{\n    V8 a = { 0 };\n    a[0] = x;\n    V8 b = a + a;\n    return b[0];\n}\n"
        "int main(void)\n{\n    return wide(3) - 6;\n}\n",
        // An always_inline callee whose target attribute its baseline caller lacks: -c's target-feature check refuses it.
        "__attribute__((target(\"avx2\"), always_inline)) static inline int twice(int x)\n{\n    return x * 2;\n}\n"
        "int main(void)\n{\n    return twice(1) - 2;\n}\n",
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const char* const names[FIXTURES] = { "vectors.c", "attribute.c", "mismatch.c" };
    static const struct
    {
        const char* label;
        uint32_t fixture;
        // A second file after the first, or -1; and an option after -march, or null.
        int32_t second;
        const char* option;
        const char* march;
        int status;
        // What its report must hold, once, and a second line it must hold once (or null).
        const char* report;
        const char* also;
    } cases[] = {
        {
            .label = "check-lanes: a unit of 512-bit vectors is measured for -march=znver5",
            .fixture = 0,
            .second = -1,
            .march = "-march=znver5",
            .status = 0,
            .report = "/vectors.c functions=2 ",
        },
        {
            .label = "check-lanes: a unit of 512-bit vectors fails for the baseline profile, as -c refuses it",
            .fixture = 0,
            .second = -1,
            .march = "-march=x86-64",
            .status = 1,
            .report = "/vectors.c failed=\"it couldn't be emitted on one lane, or its object built\"\n",
        },
        {
            .label = "check-lanes: a baseline unit's function with a target attribute takes its attribute's vectors",
            .fixture = 1,
            .second = -1,
            .march = "-march=x86-64",
            .status = 0,
            .report = "/attribute.c functions=2 ",
            .also = "/attribute.c configuration=",
        },
        {
            // #1057's review: left out as the other source benchmarks leave it out, with its receipt, and the next file
            // measured.
            .label = "check-lanes: a unit whose target features -c refuses is left out, with its receipt, and the next one measured",
            .fixture = 2,
            .second = 1,
            .march = "-march=x86-64",
            .status = 0,
            .report = "/mismatch.c rejected=\"code generation's target-feature check: ",
            .also = "/mismatch.c configuration=",
        },
        {
            .label = "check-lanes: a run that leaves out its only unit measures no file, and fails",
            .fixture = 2,
            .second = -1,
            .march = "-march=x86-64",
            .status = 1,
            .report = "check-lanes: no file was measured\n",
        },
        {
            .label = "check-lanes: a refused request fails the command with the driver's refusal, not a usage error",
            .fixture = 1,
            .second = -1,
            .option = "-fshort-enums",
            .march = "-march=x86-64",
            .status = 1,
            .report = "/attribute.c refused=\"unsupported option '-fshort-enums' (the checker does not implement its ABI layout)\"\n",
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
        char* arguments[9] = { "--passes", "1", "--lanes", "1", "-nostdinc", (char*)cases[c].march };
        uint32_t count = 6;
        if (cases[c].option != 0)
        {
            arguments[count++] = (char*)cases[c].option;
        }
        arguments[count++] = paths[cases[c].fixture];
        if (cases[c].second >= 0)
        {
            arguments[count++] = paths[cases[c].second];
        }
        int status = 0;
        char* report = 0;
        const bool ran = check_lanes_benchmark_test_run(arguments, count, &status, &report);
        const char* found = ran ? strstr(report, cases[c].report) : 0;
        const char* also = ran && cases[c].also != 0 ? strstr(report, cases[c].also) : 0;
        const bool expected = ran && status == cases[c].status && found != 0 && strstr(found + 1, cases[c].report) == 0 &&
                              (cases[c].also == 0 || (also != 0 && strstr(also + 1, cases[c].also) == 0));
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
