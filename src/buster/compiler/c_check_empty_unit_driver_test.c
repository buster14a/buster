// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wempty-translation-unit sources (#2222, c_preprocessor_impl.c's pp_diagnose_empty_unit). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_empty_unit_fixtures[] =
{
    {
        // At the main file's last newline, after the preprocessor's own warnings.
        .name = "a unit of directives alone under -pedantic",
        .source = "#define X 1\n#if 0\nint x;\n#endif\n#warning a\n",
        .options = { "-pedantic" },
        .warnings = 3,
        .compare_warning_text = true,
    },
    {
        // At its end where no newline ends it.
        .name = "a unit of a directive without a final newline",
        .source = "#define X 1",
        .options = { "-Wempty-translation-unit" },
        .warnings = 1,
        .compare_warning_text = true,
    },
    {
        .name = "an empty unit",
        .source = "",
        .options = { "-pedantic" },
        .warnings = 1,
        .compare_warning_text = true,
    },
    {
        .name = "an empty unit by default",
        .source = "\n\n",
        .compare_warning_text = true,
    },
};

static void check_test_driver_empty_unit(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_empty_unit_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_empty_unit_fixtures));
}
