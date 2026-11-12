// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for GCC's cast as an lvalue (#2264, c_check_semantic_diagnostic.c's CHECK_DIAG_LVALUE_REQUIRED
// wording). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture
// runner compares complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror and
// -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_lvalue_cast_fixtures[] =
{
    {
        // A cast of an lvalue, through parentheses, as an assignment's, a compound assignment's, an increment's or a
        // decrement's operand: clang's lvalue cast error at the cast's `(`, after the cast's own warning. A cast of an
        // rvalue and another rvalue keep "expression is not assignable" at the operator.
        .name = "lvalue casts",
        .source = "int *p;\n"
                  "int x;\n"
                  "struct S { int m; } s;\n"
                  "void f(void)\n"
                  "{\n"
                  "    (int)p = 0;\n"
                  "    (char)*p += 1;\n"
                  "    ((int)p)++;\n"
                  "    --(long)x;\n"
                  "    (int)x = 1;\n"
                  "    ((int)(x)) = 2;\n"
                  "    (int)5 = 3;\n"
                  "    (int)s.m = 4;\n"
                  "    x + 1 = 5;\n"
                  "    (short)x <<= 2;\n"
                  "}\n",
        .warnings = 2,
        .errors = 10,
    },
};

static void check_test_driver_lvalue_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_lvalue_cast_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_lvalue_cast_fixtures));
}
