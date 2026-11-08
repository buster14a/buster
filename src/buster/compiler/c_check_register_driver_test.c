// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's "address of register variable requested" (#1447): the & operator on a register object,
// its members and its arrays' elements (getPrimaryDecl), at the &, and a register array's decay where it is named
// unparenthesized (DefaultFunctionArrayConversion), at the name. The shared warning harness compares the pinned Clang's
// complete diagnostics through both kernels and requested lanes.

static const CheckDriverDefaultWarningFixture check_driver_register_fixtures[] =
{
    {
        .name = "register objects' addresses and arrays' decays",
        .source = "struct S { int m; int a[2]; };\n"
                  "void f(void) {\n"
                  "    register int x; register int arr[3]; register struct S s;\n"
                  "    int *p = &x;\n"
                  "    int *q = arr;\n"
                  "    int *r = &arr[1];\n"
                  "    int *t = &s.m;\n"
                  "    int *u = s.a;\n"
                  "    int (*v)[3] = &arr;\n"
                  "    int y = arr[0];\n"
                  "    struct S *w = &s;\n"
                  "    (void)p; (void)q; (void)r; (void)t; (void)u; (void)v; (void)y; (void)w;\n"
                  "}\n",
        .errors = 7,
    },
    {
        .name = "a register array decays at its unparenthesized name, in every operand",
        .source = "struct I { int k; };\n"
                  "struct S { int m; int a[2]; struct I in; };\n"
                  "int g(int *);\n"
                  "int h(int p[2]);\n"
                  "void f(register int prm, register int parr[2]) {\n"
                  "    register int arr[3]; register struct S s; register struct S sa[2];\n"
                  "    int n1 = sizeof(arr);\n"
                  "    int n2 = sizeof arr[0];\n"
                  "    int n3 = *arr;\n"
                  "    int n4 = *(arr + 1);\n"
                  "    int n5 = g(arr);\n"
                  "    int *p6 = &arr[0];\n"
                  "    int *p7 = (arr);\n"
                  "    __typeof__(arr) t9;\n"
                  "    int *p10 = &s.in.k;\n"
                  "    int n11 = s.a[0];\n"
                  "    int n12 = sa[0].m;\n"
                  "    int *p13 = &prm;\n"
                  "    int *p14 = parr;\n"
                  "    int *p15 = &parr[0];\n"
                  "    int n16 = sizeof(&arr);\n"
                  "    int *p17 = 1 ? arr : 0;\n"
                  "    int n18 = (arr)[1];\n"
                  "    int n19 = 1[arr];\n"
                  "    int *p20 = (int *)arr;\n"
                  "    struct S cp = s;\n"
                  "    (void)n1;(void)n2;(void)n3;(void)n4;(void)n5;(void)p6;(void)p7;(void)t9;(void)p10;(void)n11;(void)n12;(void)p13;(void)p14;(void)p15;(void)n16;(void)p17;(void)n18;(void)n19;(void)p20;(void)cp;\n"
                  "}\n",
        .errors = 12,
    },
    {
        .name = "the & operator climbs parentheses, members and array subscripts to a register object",
        .source = "struct S { int m; int a[2]; struct { int k; } in; };\n"
                  "void f(void) {\n"
                  "    register int x; register struct S s; register int arr[2]; register int *rp;\n"
                  "    int *p1 = &(x);\n"
                  "    int *p2 = &((s).m);\n"
                  "    int *p3 = &s.a[0];\n"
                  "    int (*p4)[2] = &s.a;\n"
                  "    int *p5 = &(s.in.k);\n"
                  "    int **p6 = &rp;\n"
                  "    int *p7 = &*rp;\n"
                  "    int *p8 = &(arr)[0];\n"
                  "    void *p9 = &(arr);\n"
                  "    int *p10 = (&x);\n"
                  "    int *p11 = &x + 0, *p12 = &arr[1] + 1;\n"
                  "    (void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;(void)p7;(void)p8;(void)p9;(void)p10;(void)p11;(void)p12;\n"
                  "}\n",
        .errors = 11,
    },
};

static void check_test_driver_registers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_register_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_register_fixtures));
}
