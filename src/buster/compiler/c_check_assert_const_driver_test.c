// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of a failed static assertion's note on a const object's value (#2214, c_check_semantic_diagnostic.c's
// check_semantic_operand_value). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies.

static const CheckDriverDefaultWarningFixture check_driver_assert_const_fixtures[] =
{
    {
        // "expression evaluates to" with a const object's value its initializer gives: a scalar's, in an operand's
        // arithmetic, a member's, an element's, a block's static's and a char's (as a character).
        .name = "static assertions of const objects",
        .source = "const int k = 0;\n"
                  "_Static_assert(k == 45, \"\");\n"
                  "const long m = 7;\n"
                  "_Static_assert(m + 1 == 3, \"\");\n"
                  "struct P { int a; }; const struct P p = { 4 };\n"
                  "_Static_assert(p.a == 5, \"\");\n"
                  "const int arr[2] = { 1, 2 };\n"
                  "_Static_assert(arr[1] == 5, \"\");\n"
                  "void f(void) { static const int s = 3; _Static_assert(s == 4, \"\"); }\n"
                  "const char c = 'x';\n"
                  "_Static_assert(c == 'y', \"\");\n",
        .errors = 6,
        .notes = 6,
        .syntax_only = true,
    },
    {
        // The requirement as clang's printer prints it (#2482): a compound literal's completed array bound, a _Generic's
        // `int: 1` associations, and no note where a selection's chosen operand is a literal; a record or enumeration a
        // type name defines by its name, or `(unnamed at f.c:1:2)`, without its body.
        .name = "static assertion requirements of selections, compound literals and defined tags",
        .source = "int x;\n"
                  "_Static_assert(sizeof((int[]){1, 2}) == 1, \"a\");\n"
                  "_Static_assert(sizeof((int[][2]){{1, 2}, {3}}) == 1, \"b\");\n"
                  "_Static_assert(_Generic(x, int: 1, default: 2) == 2, \"c\");\n"
                  "_Static_assert(_Generic(x, int: 1, default: 2) + 1 == 3, \"d\");\n"
                  "_Static_assert(__builtin_choose_expr(1, 3, 4) == 4, \"e\");\n"
                  "_Static_assert(__builtin_offsetof(struct { int a, b; }, b) == 1, \"f\");\n"
                  "_Static_assert(__builtin_offsetof(struct T { int a, b; }, b) == 1, \"g\");\n"
                  "_Static_assert(__builtin_offsetof(union { int a, b; }, b) == 1, \"h\");\n"
                  "_Static_assert(sizeof(enum E { A }) == 1, \"i\");\n"
                  "_Static_assert(sizeof(enum { B }) == 1, \"j\");\n",
        .errors = 10,
        .notes = 8,
        .syntax_only = true,
    },
};

static void check_test_driver_assert_const(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_assert_const_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_assert_const_fixtures));
}
