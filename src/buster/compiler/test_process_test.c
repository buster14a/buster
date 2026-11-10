// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Private self-checks included by their support root, at its original definition position.

// The calls' own checks (#465's review): a part whose output and result all come back; one that can't write its
// output (its standard output closed), which must send no result; a parent that can't write a part's output (into
// /dev/full), whose part must still send its result; and a part that exits without one, whose result is zeros. Each
// case must have started its part and made both of the parent's streams, so that it fails the way it names. Then
// test_process_share at its bounds. Prints a line and returns the failures.
static uint32_t test_process_test(void)
{
    uint32_t failures = 0;
    for (uint32_t c = 0; c < 4; c += 1)
    {
        char* output_text = 0;
        size_t output_length = 0;
        char* errors_text = 0;
        size_t errors_length = 0;
        FILE* output = c == 2 ? fopen("/dev/full", "w") : open_memstream(&output_text, &output_length);
        FILE* errors = open_memstream(&errors_text, &errors_length);
        const bool streams = output != 0 && errors != 0;
        TestProcess process = { .result = -1, .pid = -1 };
        if (streams && test_process_start(&process))
        {
            uint64_t sent = 0x5eed0000u + c;
            if (c == 1)
            {
                close(STDOUT_FILENO);
            }
            printf("output %u\n", c);
            fprintf(stderr, "errors %u\n", c);
            if (c == 3)
            {
                _exit(3);
            }
            test_process_exit(&process, &sent, sizeof(sent));
        }
        const bool started = process.pid > 0;
        uint64_t received = 1;
        const bool ran = streams && test_process_finish_to(&process, &received, sizeof(received), output, errors);
        if (output != 0)
        {
            fclose(output);
        }
        if (errors != 0)
        {
            fclose(errors);
        }
        char wanted[32];
        snprintf(wanted, sizeof(wanted), "output %u\n", c);
        char wanted_errors[32];
        snprintf(wanted_errors, sizeof(wanted_errors), "errors %u\n", c);
        const bool whole = ran && received == 0x5eed0000u && output_text != 0 && strcmp(output_text, wanted) == 0 && errors_text != 0 &&
                           strcmp(errors_text, wanted_errors) == 0;
        const bool failed = !ran && received == (c == 2 ? 0x5eed0002u : 0);
        const bool expected = streams && started && (c == 0 ? whole : failed);
        if (!expected)
        {
            printf("test processes: case %u came back %s (%s, %s)\n", c, ran ? "as run" : "as failed", streams ? "streams made" : "no streams",
                   started ? "started" : "not started");
        }
        failures += !expected;
        free(output_text);
        free(errors_text);
    }
    // The share's bounds: one CPU, fewer CPUs granted than there are, as many and more, and the most CPUs --jobs and
    // BUSTER_PARALLEL_CPUS take.
    const uint32_t shares[][3] = {
        { 1, 1, 1 },
        { 16, 1, 1 },
        { 16, 8, 8 },
        { 15, 8, 8 },
        { 16, 16, 16 },
        { 16, 17, 16 },
        { 16, UINT32_MAX, 16 },
        { UINT32_MAX, 1, 1 },
        { UINT32_MAX, UINT32_MAX, UINT32_MAX },
    };
    for (uint32_t i = 0; i < sizeof(shares) / sizeof(shares[0]); i += 1)
    {
        const uint32_t share = test_process_share(shares[i][0], shares[i][1]);
        if (share != shares[i][2])
        {
            printf("test processes: %u CPUs with %u granted gave %u, not %u\n", shares[i][0], shares[i][1], share, shares[i][2]);
        }
        failures += share != shares[i][2];
    }
    // BUSTER_PARALLEL_CPUS as the runner sets it, and values that aren't a count from 1 to UINT32_MAX, which leave every
    // CPU this process may run on: test_process_cpus with it unset. The variable is put back as it was.
    const char* saved = getenv("BUSTER_PARALLEL_CPUS");
    char* kept = saved != 0 ? strdup(saved) : 0;
    unsetenv("BUSTER_PARALLEL_CPUS");
    const uint32_t all = test_process_cpus();
    typedef struct TestProcessGrant TestProcessGrant;
    struct TestProcessGrant
    {
        const char* value;
        uint32_t expected;
    };
    const TestProcessGrant grants[] = {
        { "1", 1 },
        { "2", all < 2 ? all : 2 },
        { "4294967295", all },
        { "0", all },
        { "", all },
        { "x", all },
        { "3x", all },
        { " 3", all },
        { "+3", all },
        { "-1", all },
        { "4294967296", all },
        { "99999999999999999999999", all },
    };
    for (uint32_t i = 0; i < sizeof(grants) / sizeof(grants[0]); i += 1)
    {
        setenv("BUSTER_PARALLEL_CPUS", grants[i].value, 1);
        const uint32_t share = test_process_cpus();
        if (share != grants[i].expected)
        {
            printf("test processes: BUSTER_PARALLEL_CPUS=\"%s\" gave %u processes, not %u\n", grants[i].value, share, grants[i].expected);
        }
        failures += share != grants[i].expected;
    }
    if (kept != 0)
    {
        setenv("BUSTER_PARALLEL_CPUS", kept, 1);
    }
    else
    {
        unsetenv("BUSTER_PARALLEL_CPUS");
    }
    free(kept);
    printf("test processes: %u of 25 cases fail (output, a part's output lost, the parent's lost, a failed part, 9 shares, 12 grants)\n", failures);
    return failures;
}
