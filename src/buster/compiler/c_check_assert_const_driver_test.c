// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of a failed static assertion's note on a const object's value (#2214, c_check_semantic_diagnostic.c's
// check_semantic_operand_value). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies.

#include "c_check_assert_requirement_fallback_test.c"

static void check_test_driver_assert_const(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/assert-const", false);
    static const struct
    {
        const char* path;
        CheckDriverDefaultWarningFixture fixture;
    } files[] =
    {
        {
            .path = "driver/assert_requirement_selection.c",
            .fixture =
            {
                .name = "selected literal and conjunction requirements",
                .errors = 12,
                .notes = 6,
            },
        },
        {
            .path = "driver/assert_requirement_types.c",
            .fixture =
            {
                .name = "canonical attributed type requirements",
                .errors = 13,
            },
        },
        {
            .path = "driver/assert_requirement_ignored.c",
            .fixture =
            {
                .name = "ignored attributes disappear from requirements",
                // Existing duplicate warnings of these families are independent of requirement spelling.
                .options = { "-Wno-ignored-attributes", "-Wno-unknown-attributes" },
                .errors = 2,
            },
        },
        {
            .path = "driver/assert_requirement_controls.c",
            .fixture =
            {
                .name = "passing selected and attributed assertions",
            },
        },
        {
            .path = "driver/assert_member_extension.c",
            .fixture =
            {
                .name = "extension-prefixed member assertion recovery",
                .errors = 12,
            },
        },
        {
            .path = "driver/assert_member_extension.c",
            .fixture =
            {
                .name = "member assertion recovery under the error limit",
                .options = { "-ferror-limit=1" },
                .errors = 2,
            },
        },
        {
            .path = "driver/assert_member_extension.c",
            .fixture =
            {
                .name = "member assertion recovery under fatal errors",
                .options = { "-Wfatal-errors" },
                .errors = 1,
            },
        },
        {
            .path = "driver/assert_member_extension_macro.c",
            .fixture =
            {
                .name = "member assertion recovery retains macro notes",
                .errors = 6,
                .notes = 5,
            },
        },
        {
            .path = "driver/assert_member_extension_arguments.c",
            .fixture =
            {
                .name = "member assertion recovery retains argument origins",
                .errors = 9,
                .notes = 8,
            },
        },
        {
            .path = "driver/assert_member_extension_after.c",
            .fixture =
            {
                .name = "member assertion recovery continues after the rejected record",
                .errors = 4,
            },
        },
        {
            .path = "driver/assert_member_extension_controls.c",
            .fixture =
            {
                .name = "accepted member and extension assertion controls",
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        char path[PATH_MAX];
        const int file = check_test_fixture_path(path, sizeof(path), files[f].path) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "assertion diagnostic runtime fixture is read");
        for (uint32_t language = 0; read && language < 2; language += 1)
        {
            CheckDriverDefaultWarningFixture fixture = files[f].fixture;
            fixture.source = source;
            fixture.c23 = language != 0;
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
    check_test_driver_assert_requirement_fallback(test, clang, executable, directory);
}
