// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wshift-op-parentheses sources (#2264, c_check_common_warnings.c's check_common_shift_op_parentheses). Clang
// supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares
// complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_shift_op_parentheses_fixtures[] =
{
    {
        // An unparenthesized `+` or `-` as either operand of a `<<` or `>>`, the left one first, at it; through a macro's
        // argument with the macro's note; not a parenthesized one, a multiplication, or a compound assignment's.
        .name = "additions in shifts",
        .source = "int a(int x, int b, int c) { return x << b + c; }\n"
                  "int b(int x, int b, int c) { return x + b << c; }\n"
                  "int c(int x, int b, int c) { return x >> b - c; }\n"
                  "int d(int x, int b, int c) { return x << (b + c) | (x + b) << c | x << b * c; }\n"
                  "int e(int x, int b, int c) { return x - b >> c + 1; }\n"
                  "#define SH(a, b) (a << b)\n"
                  "int f(int x, int b, int c) { return SH(x, b + c); }\n"
                  "#define ID(a) a\n"
                  "int g(int x) { return ID(x << x + 1) + (ID(x) << x + 1); }\n"
                  "int h(int x, int b) { x <<= b + 1; return x << b + 1 << 2; }\n",
        .warnings = 9,
        .notes = 11,
    },
    {
        // In constant contexts, beside the shift's own count warning, in an unevaluated operand.
        .name = "additions in shifts in other places",
        .source = "int g = 1 << 2 + 1;\n"
                  "int b(int x) { return 1 << 40 + x; }\n"
                  "unsigned long c(int x) { return sizeof(x << x + 1); }\n"
                  "enum { E = 1 << 3 - 1 };\n"
                  "int e(int x) { switch (x) { case 1 << 1 + 1: return 1; } return x >> (x) - 1; }\n",
        .warnings = 6,
        .notes = 6,
    },
};

static void check_test_driver_shift_op_parentheses(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_shift_op_parentheses_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_shift_op_parentheses_fixtures));
}
