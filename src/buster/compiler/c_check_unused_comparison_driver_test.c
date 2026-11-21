// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #2906 sources. Clang is the external oracle for the comparison warning and its optional assignment note.
// Include after c_check_warning_driver_test.c; its fixture runner compares complete diagnostics and exit status through
// both readers and lane requests, under default, -Werror and -w policy, in syntax and object modes.

typedef struct CheckDriverUnusedComparisonFile CheckDriverUnusedComparisonFile;
struct CheckDriverUnusedComparisonFile
{
    const char* path;
    CheckDriverDefaultWarningFixture fixture;
};

static bool check_test_driver_failed_comparison_output(const char* text)
{
    static const char failure[] = "error: static assertion failed";
    uint32_t failures = 0;
    const char* cursor = text;
    while (cursor != NULL && (cursor = strstr(cursor, failure)) != NULL)
    {
        failures += 1;
        cursor += sizeof(failure) - 1;
    }
    const bool result = text != NULL && failures == 10 &&
                        strstr(text, "use '=' to turn this equality comparison into an assignment") == NULL &&
                        strstr(text, "use '|=' to turn this inequality comparison into an or-assignment") == NULL;
    return result;
}

static void check_test_driver_failed_comparisons(CheckTest* test, const char* clang, const char* executable)
{
    // #2915 supplies complete failed-assertion comma-warning recovery. Do not freeze its currently missing warnings:
    // before and after that recovery lands, all ten assertions must fail and no constant comparison may suggest an
    // assignment. The same real source is also suitable for a complete-output comparison with that recovery present.
    static const char* const policies[] =
    {
        NULL,
        "-Werror",
        "-w",
    };
    for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
    {
        char* arguments[12] =
        {
            (char*)clang,
            "-std=gnu17",
            "-fsyntax-only",
            "-fno-caret-diagnostics",
            "-fno-color-diagnostics",
            "-ferror-limit=0",
            "tests/driver/unused_comparison_failed_assertions.c",
        };
        uint32_t count = 7;
        if (policies[policy] != NULL)
        {
            arguments[count++] = (char*)policies[policy];
        }
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        check_test_expect(test, oracle == 1 && check_test_driver_failed_comparison_output(expected),
                          "independent Clang oracle rejects failed comparison assertions without assignment notes");
        for (uint32_t lanes = 0; lanes < 2; lanes += 1)
        {
            arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
            arguments[count] = "--lanes";
            arguments[count + 1] = lanes != 0 ? "3" : "1";
            arguments[count + 2] = NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            char description[160];
            snprintf(description, sizeof(description), "failed comparison assertions policy=%u, %s lanes: errors retained without assignment notes",
                     policy, lanes != 0 ? "3" : "1");
            check_test_expect(test, oracle == 1 && status == 1 && check_test_driver_failed_comparison_output(actual), description);
            free(actual);
        }
        free(expected);
    }
}

static void check_test_driver_unused_comparisons(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverUnusedComparisonFile files[] =
    {
        {
            .path = "tests/driver/unused_comparison_values.c",
            .fixture =
            {
                .name = "unused comparison notes follow the left operand's source value category",
                .options = { "-ferror-limit=0" },
                .warnings = 24,
                .notes = 13,
            },
        },
        {
            .path = "tests/driver/unused_comparison_categories.c",
            .fixture =
            {
                .name = "unused comparison notes retain selections and reject duplicate vector components",
                .options = { "-ferror-limit=0" },
                .warnings = 9,
                .notes = 5,
                // Vector member lowering is separate from these source-category diagnostics.
                .syntax_only = true,
            },
        },
        {
            .path = "tests/driver/unused_comparison_comma.c",
            .fixture =
            {
                .name = "discarded comma comparisons use the same assignment-note eligibility",
                .options = { "-ferror-limit=0" },
                .warnings = 10,
                .notes = 4,
            },
        },
        {
            .path = "tests/driver/unused_comparison_assertions.c",
            .fixture =
            {
                .name = "successful assertions suppress constant comparison notes and retain lvalue notes",
                .options = { "-ferror-limit=0" },
                .warnings = 11,
                .notes = 2,
            },
        },
        {
            .path = "tests/driver/unused_comparison_policy.c",
            .fixture =
            {
                .name = "unused comparison notes respect macro operands and scoped warning policy",
                .options = { "-ferror-limit=0" },
                .warnings = 10,
                .notes = 3,
            },
        },
        {
            .path = "tests/driver/unused_comparison_rejected.c",
            .fixture =
            {
                .name = "failed assertions retain preceding comparison diagnostics and preserve rejected output",
                .options = { "-ferror-limit=0" },
                .warnings = 3,
                .errors = 2,
                .notes = 1,
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        const int file = open(files[f].path, O_RDONLY | O_CLOEXEC);
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "unused comparison runtime fixture is read (run from the repository root)");
        if (read)
        {
            CheckDriverDefaultWarningFixture fixture = files[f].fixture;
            fixture.source = source;
            for (uint32_t language = 0; language < 2; language += 1)
            {
                fixture.c23 = language != 0;
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            }
            if (f == 0)
            {
                // Both the child warning and its unused-value parent suppress their notes; a later child enable restores
                // exactly the comparison diagnostics. The runner applies -Werror and -w to each of these option lists too.
                static const char* const disabled[] =
                {
                    "-Wno-unused-comparison",
                    "-Wno-unused-value",
                };
                fixture.c23 = false;
                fixture.warnings = 0;
                fixture.notes = 0;
                for (uint32_t option = 0; option < BUSTER_ARRAY_LENGTH(disabled); option += 1)
                {
                    fixture.options[1] = disabled[option];
                    check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
                }
                fixture.options[2] = "-Wunused-comparison";
                fixture.warnings = files[f].fixture.warnings;
                fixture.notes = files[f].fixture.notes;
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            }
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
    check_test_driver_failed_comparisons(test, clang, executable);
}
