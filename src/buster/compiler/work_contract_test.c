// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The work-contract record's verdict and failure evidence. The operation contracts use this same helper; these
// boundary tests complement their real excessive-work mutations, rather than standing in for those mutations.

#include <string.h>

#include "work_contract.h"

typedef struct WorkReportCase WorkReportCase;
struct WorkReportCase
{
    WorkContract contract;
    bool passed;
    const char* record;
};

static uint32_t work_report_checks;
static uint32_t work_report_failures;

static void work_report_expect(bool condition, const char* description)
{
    work_report_checks += 1;
    if (!condition)
    {
        work_report_failures += 1;
        fprintf(stderr, "work-contract report: FAIL: %s\n", description);
    }
}

static void work_report_case(const WorkReportCase* test)
{
    FILE* output = tmpfile();
    work_report_expect(output != NULL, "capture stream is available");
    if (output != NULL)
    {
        const bool passed = work_contract_report(output, &test->contract);
        work_report_expect(passed == test->passed, test->contract.fixture);
        const bool rewound = fseek(output, 0, SEEK_SET) == 0;
        char record[512];
        const size_t length = rewound ? fread(record, 1, sizeof(record) - 1, output) : 0;
        record[length] = 0;
        work_report_expect(rewound && !ferror(output) && strcmp(record, test->record) == 0, "record retains the fixture, size, metric, count, bound and both verdicts");
        work_report_expect(fclose(output) == 0, "capture stream closes successfully");
    }
}

static void work_report_write_failure(bool buffered)
{
    const WorkContract contract = {
        .fixture = "write-failure",
        .metric = "visits",
        .input = 8,
        .actual = 8,
        .bound = 8,
        .semantic = true,
    };
    FILE* output = fopen("/dev/full", "w");
    work_report_expect(output != NULL, "failing output device is available");
    if (output != NULL)
    {
        char buffer[512];
        const bool configured = setvbuf(output, buffered ? buffer : NULL, buffered ? _IOFBF : _IONBF, buffered ? sizeof(buffer) : 0) == 0;
        work_report_expect(configured, "output buffering is configured");
        if (configured)
        {
            work_report_expect(!work_contract_report(output, &contract) && ferror(output), buffered ? "a buffered write failure fails the contract" : "an immediate write failure fails the contract");
        }
        fclose(output);
    }
}

static int work_contract_report_tests(void)
{
    static const WorkReportCase tests[] = {
        {
            .contract = {
                .fixture = "boundary",
                .metric = "visits",
                .input = 64,
                .actual = 128,
                .bound = 128,
                .semantic = true,
            },
            .passed = true,
            .record = "work-contract fixture=boundary size=64 metric=visits actual=128 bound=128 semantic=pass result=pass\n",
        },
        {
            .contract = {
                .fixture = "excessive-work",
                .metric = "probes",
                .input = 64,
                .actual = 129,
                .bound = 128,
                .semantic = true,
            },
            .passed = false,
            .record = "work-contract fixture=excessive-work size=64 metric=probes actual=129 bound=128 semantic=pass result=fail\n",
        },
        {
            .contract = {
                .fixture = "empty",
                .metric = "bytes",
                .input = 0,
                .actual = 0,
                .bound = 0,
                .semantic = true,
            },
            .passed = true,
            .record = "work-contract fixture=empty size=0 metric=bytes actual=0 bound=0 semantic=pass result=pass\n",
        },
        {
            .contract = {
                .fixture = "omitted-output",
                .metric = "visits",
                .input = 128,
                .actual = 0,
                .bound = 1024,
                .semantic = false,
            },
            .passed = false,
            .record = "work-contract fixture=omitted-output size=128 metric=visits actual=0 bound=1024 semantic=fail result=fail\n",
        },
        {
            .contract = {
                .fixture = "uint64-boundary",
                .metric = "bytes",
                .input = UINT64_MAX,
                .actual = UINT64_MAX,
                .bound = UINT64_MAX,
                .semantic = true,
            },
            .passed = true,
            .record = "work-contract fixture=uint64-boundary size=18446744073709551615 metric=bytes actual=18446744073709551615 bound=18446744073709551615 semantic=pass result=pass\n",
        },
        {
            .contract = {
                .fixture = "uint64-excessive",
                .metric = "bytes",
                .input = UINT64_MAX,
                .actual = UINT64_MAX,
                .bound = UINT64_MAX - 1,
                .semantic = true,
            },
            .passed = false,
            .record = "work-contract fixture=uint64-excessive size=18446744073709551615 metric=bytes actual=18446744073709551615 bound=18446744073709551614 semantic=pass result=fail\n",
        },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i += 1)
    {
        work_report_case(&tests[i]);
    }
    work_report_write_failure(false);
    work_report_write_failure(true);
    printf("work-contract report: %u checks, %u failures\n", work_report_checks, work_report_failures);
    return work_report_failures != 0;
}
