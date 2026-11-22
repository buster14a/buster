// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of a static assertion's condition converted to a boolean (#2409, c_check_common_warnings.c's
// check_common_assertion_reach and c_checker_driver.c's failed-assertion publication). Clang supplies the external
// diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret
// diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

typedef struct CheckDriverAssertFile CheckDriverAssertFile;
struct CheckDriverAssertFile
{
    const char* path;
    CheckDriverDefaultWarningFixture fixture;
};

static const CheckDriverDefaultWarningFixture check_driver_assert_bool_fixtures[] =
{
    {
        // A `<<` of literals and a conditional of integer literals as a static assertion's condition, at file scope, among
        // a record's members and in a block, where the shift's count gives no warning of its own; a runtime one's does.
        .name = "static assertions' conditions converted to booleans",
        .source = "_Static_assert(1 << 40, \"big\");\n"
                  "struct Y { int a; _Static_assert(1 << 40, \"big\"); };\n"
                  "void f(void) { _Static_assert(1 << 40, \"big\"); if (1 << 40) {} }\n"
                  "_Static_assert(1 << 4, \"a\");\n"
                  "_Static_assert(2 ? 3 : 4, \"c\");\n"
                  "_Static_assert(1 ? 1 : 0, \"d\");\n"
                  "_Static_assert((1 << 4) != 0, \"e\");\n",
        .warnings = 7,
        .compare_warning_text = true,
    },
};

static void check_test_driver_assert_bool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_assert_bool_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_assert_bool_fixtures));
    // Failed assertions never reach the ordinary warning walk. The runtime sources also retain successful assertions,
    // plain failures and an explicit comparison, so recovery must neither repeat a warning nor invent one for a control.
    static const CheckDriverAssertFile failed[] =
    {
        {
            .path = "tests/driver/assert_bool_comma.c",
            .fixture =
            {
                .name = "failed assertions retain comma warnings in source and deferred order",
                .options = { "-ferror-limit=0" },
                .warnings = 23,
                .errors = 20,
                .notes = 5,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma.c",
            .fixture =
            {
                .name = "failed assertion comma warnings retain C23 behavior",
                .options = { "-ferror-limit=0" },
                .warnings = 23,
                .errors = 20,
                .notes = 5,
                .c23 = true,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma.c",
            .fixture =
            {
                .name = "failed assertion comma warnings precede the error limit",
                .options = { "-ferror-limit=1" },
                .warnings = 3,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma.c",
            .fixture =
            {
                .name = "failed assertion comma warnings precede fatal errors",
                .options = { "-Wfatal-errors" },
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma_block.c",
            .fixture =
            {
                .name = "failed assertion comma warnings keep lexical and function-end phases",
                .warnings = 3,
                .errors = 3,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma_block.c",
            .fixture =
            {
                .name = "fatal assertion errors suppress later deferred comma warnings",
                .options = { "-Wfatal-errors" },
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_comma_policy.c",
            .fixture =
            {
                .name = "failed assertion comma warnings retain pragma and macro policy",
                .warnings = 2,
                .errors = 4,
                .notes = 6,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached.c",
            .fixture =
            {
                .name = "failed assertions retain detached selection condition warnings",
                .warnings = 9,
                .errors = 14,
                .notes = 4,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached.c",
            .fixture =
            {
                .name = "failed detached condition warnings retain C23 behavior",
                .warnings = 9,
                .errors = 14,
                .notes = 4,
                .c23 = true,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached.c",
            .fixture =
            {
                .name = "detached condition warnings precede the assertion error limit",
                .options = { "-ferror-limit=1" },
                .warnings = 2,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached.c",
            .fixture =
            {
                .name = "detached condition warnings precede a fatal assertion error",
                .options = { "-Wfatal-errors" },
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached_nested.c",
            .fixture =
            {
                .name = "a nested failed assertion owns its condition and comma warnings",
                .warnings = 2,
                .errors = 4,
            },
        },
        {
            .path = "tests/driver/assert_bool_detached_policy.c",
            .fixture =
            {
                .name = "detached condition warnings retain pragma and macro policy",
                .warnings = 2,
                .errors = 3,
                .notes = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested.c",
            .fixture =
            {
                .name = "failed assertions retain nested boolean conversions in order",
                // The separate constant-logical-operand warning family is outside this recovery path.
                .options = { "-Wno-constant-logical-operand" },
                .warnings = 23,
                .errors = 15,
                .notes = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested.c",
            .fixture =
            {
                .name = "failed nested assertion conversions retain C23 and enabled signed shifts",
                .options = { "-Wno-constant-logical-operand", "-Wint-in-bool-context" },
                .warnings = 24,
                .errors = 15,
                .notes = 2,
                .c23 = true,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested.c",
            .fixture =
            {
                .name = "failed nested assertion warnings precede the error limit",
                .options = { "-Wno-constant-logical-operand", "-ferror-limit=1" },
                .warnings = 3,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested.c",
            .fixture =
            {
                .name = "failed nested assertion warnings precede fatal errors",
                .options = { "-Wno-constant-logical-operand", "-Wfatal-errors" },
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested_controls.c",
            .fixture =
            {
                .name = "failed assertion traversal respects explicit casts and unevaluated operands",
                .errors = 7,
                .notes = 3,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested_deep.c",
            .fixture =
            {
                .name = "failed assertion traversal grows its frontier and follows builtin arguments",
                .warnings = 4,
                .errors = 3,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_nested_policy.c",
            .fixture =
            {
                .name = "failed nested assertion warnings keep pragma and macro policy",
                .warnings = 3,
                .errors = 3,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_failed.c",
            .fixture =
            {
                .name = "failed assertions retain boolean warnings in every scope",
                .warnings = 5,
                .errors = 6,
                .notes = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_failed.c",
            .fixture =
            {
                .name = "failed assertions retain boolean warnings in C23",
                .warnings = 5,
                .errors = 6,
                .notes = 1,
                .c23 = true,
            },
        },
        {
            .path = "tests/driver/assert_bool_failed.c",
            .fixture =
            {
                .name = "failed assertion warnings precede the error limit",
                .options =
                {
                    "-ferror-limit=1",
                },
                .warnings = 2,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_failed.c",
            .fixture =
            {
                .name = "failed assertion warnings precede fatal errors",
                .options =
                {
                    "-Wfatal-errors",
                },
                .warnings = 1,
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_bool_policy.c",
            .fixture =
            {
                .name = "failed assertion warnings follow source policy",
                .warnings = 2,
                .errors = 5,
            },
        },
        {
            .path = "tests/driver/assert_bool_default_ignored.c",
            .fixture =
            {
                .name = "failed assertion signed shift warnings are ignored by default",
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_default_ignored.c",
            .fixture =
            {
                .name = "failed assertion signed shift warnings can be enabled",
                .options =
                {
                    "-Wint-in-bool-context",
                },
                .warnings = 1,
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_macro.c",
            .fixture =
            {
                .name = "failed assertion warnings keep macro expansion notes",
                .warnings = 1,
                .errors = 1,
                .notes = 2,
            },
        },
        {
            .path = "tests/driver/assert_bool_recovery.c",
            .fixture =
            {
                .name = "failed assertion warnings coexist with unrelated errors",
                .warnings = 3,
                .errors = 4,
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(failed); f += 1)
    {
        const int file = open(failed[f].path, O_RDONLY | O_CLOEXEC);
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "failed assertion runtime fixture is read (run from the repository root)");
        if (read)
        {
            CheckDriverDefaultWarningFixture fixture = failed[f].fixture;
            fixture.source = source;
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
}
