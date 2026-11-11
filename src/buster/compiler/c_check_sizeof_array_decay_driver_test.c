// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wsizeof-array-decay sources (#2264, c_check_common_warnings.c's check_common_sizeof_array_decay). Clang supplies
// the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_sizeof_array_decay_fixtures[] =
{
    {
        // A comma, an addition, a subtraction or an assignment whose operand decays to its type, through parentheses, each
        // such operand once; a member array, a dereferenced array pointer; not where the operator's type differs.
        .name = "sizeof of operators on decayed arrays",
        .source = "int a[10];\n"
                  "int b[3];\n"
                  "unsigned long f(int x) { return sizeof(0, a) + sizeof(0, 1, a) + sizeof(a, 0) + sizeof((0, a)) + sizeof(x, b, a) + sizeof(b, a); }\n"
                  "void g(int (*q)[10], int *p) { (void)sizeof(*q + 1); (void)sizeof(p += 0); (void)sizeof(a[0] + 1); (void)sizeof(p = a); }\n"
                  "unsigned long h(void) { struct { int m[4]; } s; return sizeof(s.m + 1) + sizeof((s).m - 1) + sizeof(1 + a); }\n",
        .warnings = 13,
    },
    {
        // At file scope, in a macro, a typedef's spelling, a variable length and a const array; not alignof's operand, an
        // unparenthesized operand, an array parameter or a pointer.
        .name = "sizeof of operators on decayed arrays in other places",
        .source = "int a[10];\n"
                  "typedef int A[3];\n"
                  "A t;\n"
                  "unsigned long z = sizeof(a + 1);\n"
                  "#define S(x) sizeof(x + 1)\n"
                  "unsigned long f(int n, int p[3]) { int v[n]; const int c[2] = {0}; return S(a) + sizeof(t + 1) + sizeof(v + 1) + sizeof(c + 1) + "
                  "__alignof__(a + 1) + sizeof a + 1 + sizeof(p + 1) + sizeof(&a[0] + 1); }\n",
        .warnings = 5,
        .notes = 1,
    },
};

static void check_test_driver_sizeof_array_decay(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_sizeof_array_decay_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_sizeof_array_decay_fixtures));
}
