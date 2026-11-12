// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Winteger-overflow sources (#2217, c_checker_driver.c's check_driver_overflow_message). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_integer_overflow_fixtures[] =
{
    {
        // The wrapped result at every width the graph folds, grouped by three past three digits, and its type: an operand's
        // typedef when only that operand has the result's type, canonical when both do.
        .name = "overflow results and types",
        .source = "typedef int T;\n"
                  "typedef long L;\n"
                  "int x = 0x7fffffff * 2;\n"
                  "long y = 0x7fffffffffffffffL * 2;\n"
                  "__int128 z = (__int128)0x7fffffffffffffffL * 0x7fffffffffffffffL * 4;\n"
                  "T a = (T)0x7fffffff * 2;\n"
                  "int b = (L)0x7fffffffffffffffL + 1 > 0;\n"
                  "int e = -(-2147483647 - 1);\n"
                  "int h = -2147483647 - 1 - 1;\n"
                  "unsigned long long q = 999999999LL * 999999999LL * 1000;\n"
                  "int f(int v) { int k = 2147483647 * 3 + v; return (1 << 31) + (3 << 30) + k; }\n",
        .warnings = 10,
        .compare_warning_text = true,
    },
    {
        // _BitInt results, past 128 bits too (check_wide_text's digits). Syntax only: clang's -c cannot compile a
        // _BitInt(70) static initializer.
        .name = "overflow results of bit-precise types",
        .source = "_BitInt(70) w = (_BitInt(70))0x1fffffffffffffffffwb * 4;\n"
                  "_BitInt(200) c = ((_BitInt(200))1 << 198) * 2;\n"
                  "signed _BitInt(9) s = (signed _BitInt(9))255 * 2;\n",
        .warnings = 3,
        .c23 = true,
        .syntax_only = true,
        .compare_warning_text = true,
    },
    {
        // What only the constant reader reads: an enumerator's value, a static assertion's condition, a case label.
        .name = "overflow results the constant reader folds",
        .source = "enum { E = 0x7fffffff + 1 };\n"
                  "enum { E2 = (long)0x7fffffffffffffffL * 3 > 0 };\n"
                  "_Static_assert(0x7fffffff + 1, \"\");\n"
                  "int g(int v) { switch (v) { case 0x7fffffff + 1: return 1; } return 0x7fffffff + 2; }\n",
        .warnings = 5,
        .compare_warning_text = true,
    },
};

static void check_test_driver_integer_overflow(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_integer_overflow_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_integer_overflow_fixtures));
}
