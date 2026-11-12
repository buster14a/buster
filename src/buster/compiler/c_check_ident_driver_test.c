// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of #ident's and #sccs's -Wpedantic warning (#2222, c_preprocessor_impl.c's PP_DIRECTIVE_IDENT). Clang
// supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares
// complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_ident_fixtures[] =
{
    {
        // At the directive's name, not in a skipped group, nor where a pragma ignores -Wpedantic.
        .name = "#ident under -pedantic",
        .source = "#ident \"x\"\nint a;\n#sccs \"y\"\n#if 0\n#ident \"z\"\n#endif\n"
                  "#pragma clang diagnostic ignored \"-Wpedantic\"\n#ident \"q\"\n",
        .options = { "-pedantic" },
        .warnings = 2,
        .compare_warning_text = true,
    },
    {
        .name = "#ident by default",
        .source = "#ident \"x\"\nint a;\n",
        .compare_warning_text = true,
    },
};

static void check_test_driver_ident(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_ident_fixtures, BUSTER_ARRAY_LENGTH(check_driver_ident_fixtures));
}
