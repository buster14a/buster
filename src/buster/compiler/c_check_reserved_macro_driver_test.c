// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wreserved-macro-identifier sources (#2222, c_preprocessor_impl.c's pp_reserved_macro_name). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_reserved_macro_fixtures[] =
{
    {
        // A #define's reserved name (`__x`, `_X`) and an #undef's, but not a feature-test macro clang lists, a lowercase or
        // a single underscore's.
        .name = "reserved macro names and the feature-test macros",
        .source = "#define _GNU_SOURCE\n"
                  "#define _XOPEN_SOURCE 700\n"
                  "#define _POSIX_C_SOURCE 200809L\n"
                  "#define __STDC_WANT_LIB_EXT1__ 1\n"
                  "#define _FORTIFY_SOURCE 2\n"
                  "#define _DEFAULT_SOURCE\n"
                  "#define _BSD_SOURCE\n"
                  "#define _ISOC99_SOURCE\n"
                  "#define _LARGEFILE64_SOURCE\n"
                  "#define _FILE_OFFSET_BITS 64\n"
                  "#define __STDC_FORMAT_MACROS\n"
                  "#define _lower 1\n"
                  "#define __x 1\n"
                  "#define x__y 1\n"
                  "#define _ 1\n"
                  "#undef _Foo\n"
                  "#define _REENTRANT\n"
                  "#define _THREAD_SAFE\n"
                  "#define __STDC_CONSTANT_MACROS\n"
                  "int z;\n",
        .options = { "-Wreserved-macro-identifier" },
        .warnings = 5,
    },
    {
        // An #undef of a feature-test macro, and before a definition's whitespace warning and an #undef's extra tokens.
        .name = "reserved macro names before the directives' other warnings",
        .source = "#undef _GNU_SOURCE\n"
                  "#undef __STDC_FORMAT_MACROS\n"
                  "#undef _Foo\n"
                  "#define _Bar(x) x\n"
                  "#define __y@\n"
                  "#define _Baz 1 2\n"
                  "#undef _Q extra\n"
                  "int z;\n",
        .options = { "-Wreserved-identifier" },
        .warnings = 9,
    },
    {
        // None from a system header.
        .name = "reserved macro names in system headers",
        .source = "#include <stddef.h>\n"
                  "#include <stdint.h>\n"
                  "#define __mine 1\n"
                  "int z;\n",
        .options = { "-Wreserved-identifier" },
        .warnings = 1,
    },
    {
        // Off by default; a pragma enables and ignores it, by its own name or its group's.
        .name = "reserved macro names under pragmas",
        .source = "#define _A 1\n"
                  "#pragma clang diagnostic warning \"-Wreserved-macro-identifier\"\n"
                  "#define _B 1\n"
                  "#undef _B\n"
                  "#pragma clang diagnostic ignored \"-Wreserved-identifier\"\n"
                  "#define _C 1\n"
                  "int z;\n",
        .warnings = 2,
    },
};

static void check_test_driver_reserved_macro(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_reserved_macro_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_reserved_macro_fixtures));
}
