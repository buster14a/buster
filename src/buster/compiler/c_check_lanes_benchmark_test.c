// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-check-lanes' preparation (#1045): the program it emits is the one -c compiles for the target its options give.
// An original fixture of 512-bit vectors, which the baseline x86-64 profile refuses ("vector results wider than the
// target's registers") and znver5 takes, is run through the command on one lane and one pass: with -march=znver5 it must
// be measured, and without it fail at its emission, as -c would. Each case's exit status and report line are checked.
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

static void check_test_check_lanes_benchmark(CheckTest* test)
{
    // Two functions that return and add 512-bit vectors, which -c lowers for znver5 and refuses for the baseline.
    static const char fixture[] = "typedef int V __attribute__((vector_size(64)));\n"
                                  "V add(V a, V b)\n{\n    return a + b;\n}\n"
                                  "int first(V a)\n{\n    V c = add(a, a);\n    return c[0];\n}\n";
    static const struct
    {
        const char* label;
        const char* march;
        int status;
        // What its report must hold, once.
        const char* report;
    } cases[] = {
        {
            .label = "check-lanes: a unit of 512-bit vectors is measured for -march=znver5",
            .march = "-march=znver5",
            .status = 0,
            .report = "/vectors.c functions=2 ",
        },
        {
            .label = "check-lanes: a unit of 512-bit vectors fails for the baseline profile, as -c refuses it",
            .march = "-march=x86-64",
            .status = 1,
            .report = "/vectors.c failed=\"it couldn't be emitted on one lane, or its object built\"\n",
        },
    };
    char directory[] = "/tmp/buster-check-lanes-XXXXXX";
    char path[256];
    bool ready = mkdtemp(directory) != 0;
    snprintf(path, sizeof(path), "%s/vectors.c", directory);
    ready = ready && parse_test_write(path, fixture, strlen(fixture));
    check_test_expect(test, ready, "check-lanes benchmark fixture is written");
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* arguments[] = { "--passes", "1", "--lanes", "1", "-nostdinc", (char*)cases[c].march, path };
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
    unlink(path);
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
