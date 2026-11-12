// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original chained comparison sources (#2264, c_check_common_warnings.c's check_common_chained_comparisons): clang's
// -Wparentheses error by default. Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_chained_comparison_fixtures[] =
{
    {
        // A relational comparison whose left operand is one, unparenthesized, at the outer operator, an error that -w
        // leaves; not an equality's, a parenthesized one or a relational right operand.
        .name = "chained comparisons",
        .source = "int x = 1 < 2 < 3;\n"
                  "int a(int p, int q, int r) { return p < q < r; }\n"
                  "int b(int p, int q, int r) { return p <= q > r; }\n"
                  "int c(int p, int q, int r) { return p == q == r || p < q == r || (p < q) < r || p == q < r || p != q != r; }\n"
                  "int g(int p, int q, int r, int s) { return p < q < r < s; }\n",
        .errors = 5,
    },
    {
        .name = "chained comparisons demoted",
        .source = "int a(int p, int q, int r) { return p >= q >= r; }\n",
        .options = { "-Wno-error=parentheses" },
        .warnings = 1,
    },
};

static void check_test_driver_chained_comparisons(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_chained_comparison_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_chained_comparison_fixtures));
}
