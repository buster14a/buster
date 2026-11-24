// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original string-addition sources (#2190 and #2406, c_check_common_warnings.c's check_common_string_plus_int). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static void check_test_driver_string_plus_int(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/string-plus-int", false);
    // The character warning is independently controlled and also diagnoses character pointer variables. Its runtime
    // sources keep the sibling ordering, direct-literal boundary, character values and rejected-unit continuation.
    static const struct
    {
        const char* file;
        CheckDriverDefaultWarningFixture fixture;
    } cases[] =
    {
        {
            "literals.c",
            {
                .name = "characters added to string literals",
                .warnings = 14,
                .notes = 16,
            },
        },
        {
            "pointers.c",
            {
                .name = "characters added to character pointers",
                .warnings = 12,
                .notes = 12,
            },
        },
        {
            "values.c",
            {
                .name = "character addition diagnostic types",
                .warnings = 12,
                .notes = 10,
                .c23 = true,
            },
        },
        {
            "rejected.c",
            {
                .name = "string additions around a rejected operand",
                .warnings = 3,
                .errors = 1,
                .notes = 3,
            },
        },
        {
            "pragmas.c",
            {
                .name = "independent string addition pragmas",
                .warnings = 4,
                .notes = 4,
            },
        },
        {
            "policy.c",
            {
                .name = "string-plus-char with string-plus-int disabled",
                .options = { "-Wno-string-plus-int" },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "string-plus-int with string-plus-char disabled",
                .options = { "-Wno-string-plus-char" },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "enabling integer addition does not enable character addition",
                .options = {
                    "-Wno-string-plus-char",
                    "-Wstring-plus-int",
                },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "disabling character addition does not disable integer addition",
                .options = {
                    "-Wstring-plus-int",
                    "-Wno-string-plus-char",
                },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "enabling character addition does not enable integer addition",
                .options = {
                    "-Wno-string-plus-int",
                    "-Wstring-plus-char",
                },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "disabling integer addition does not disable character addition",
                .options = {
                    "-Wstring-plus-char",
                    "-Wno-string-plus-int",
                },
                .warnings = 1,
                .notes = 1,
            },
        },
        {
            "policy.c",
            {
                .name = "character error and integer warning",
                .options = {
                    "-Werror=string-plus-char",
                    "-Wno-error=string-plus-int",
                },
                .warnings = 1,
                .errors = 1,
                .notes = 2,
            },
        },
        {
            "policy.c",
            {
                .name = "integer warning and character error",
                .options = {
                    "-Wno-error=string-plus-int",
                    "-Werror=string-plus-char",
                },
                .warnings = 1,
                .errors = 1,
                .notes = 2,
            },
        },
    };
    CheckTestFixtures storage = { .storage = arena_reserve(UINT64_C(1) << 20) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/checker/string-plus-char/%s", check_test_fixture_root, cases[i].file);
        uint64_t length = 0;
        CheckDriverDefaultWarningFixture fixture = cases[i].fixture;
        fixture.source = check_test_fixture_read(&storage, path, &length);
        const bool ready = fixture.source != NULL && length != 0;
        check_test_expect(test, ready, "runtime string-plus-char source is readable");
        if (ready)
        {
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
    }
    arena_release(&storage.storage);
}
