// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Type sugar in diagnostics (#1447): original fixtures whose whole diagnostic output is compared with clang's on both
// paths, at each warning policy, with -fsyntax-only and -c, on one and three lanes. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static const CheckDriverDefaultWarningFixture check_driver_type_sugar_fixtures[] =
{
    {
        // A dereference's or subscript's operand keeps the typedef its object's declaration chain names at that level
        // (check_driver_spelling_chain): a vector reached through a pointer typedef prints its own typedef, a typedef
        // inside a typedef its inner name, a parameter's array typedef its element's.
        .name = "typedef chains through dereferences and subscripts",
        .source = "typedef int v4si __attribute__((vector_size(16)));\n"
                  "typedef v4si *pv;\n"
                  "typedef pv *pvp;\n"
                  "typedef void *VP;\n"
                  "typedef VP *VPP;\n"
                  "typedef int I;\n"
                  "typedef I *IP;\n"
                  "typedef IP IPA[2];\n"
                  "void f(pv p, pvp pp, VPP b, IPA a, IP *q, v4si *direct)\n"
                  "{\n"
                  "    (*p)++;\n"
                  "    p[1]--;\n"
                  "    ++**pp;\n"
                  "    (*pp)();\n"
                  "    (void)**b;\n"
                  "    a[0]();\n"
                  "    (*a[1])();\n"
                  "    (*q)();\n"
                  "    (**q)();\n"
                  "    (*direct)++;\n"
                  "}\n",
        .warnings = 1,
        .errors = 9,
    },
    {
        // A member's operand keeps its member declarator's typedef (check_semantic_member_token): through `.`, `->`,
        // anonymous members, a member's own dereference and subscript, and a const record's or member's qualifiers.
        .name = "member typedefs in operand errors",
        .source = "typedef int I;\n"
                  "struct u { int a; };\n"
                  "typedef struct u uu;\n"
                  "typedef uu *uup;\n"
                  "typedef int v4si __attribute__((vector_size(16)));\n"
                  "struct s\n"
                  "{\n"
                  "    uu f;\n"
                  "    uup g;\n"
                  "    I arr[2];\n"
                  "    struct { uu deep; I *ip; };\n"
                  "    union { v4si v; float fl; } un;\n"
                  "    const uu cf;\n"
                  "    struct s *next;\n"
                  "};\n"
                  "void g(struct s x, struct s *px, const struct s cx)\n"
                  "{\n"
                  "    x.f++;\n"
                  "    px->f++;\n"
                  "    (*x.g)++;\n"
                  "    x.g->a();\n"
                  "    x.arr[0]();\n"
                  "    x.deep--;\n"
                  "    (*px->ip)();\n"
                  "    x.un.v++;\n"
                  "    cx.f++;\n"
                  "    x.cf++;\n"
                  "    px->next->next->f++;\n"
                  "    (x).f();\n"
                  "    x.next->ip();\n"
                  "}\n",
        .errors = 13,
    },
    {
        // A typedef of a qualified type names the element of its array (type_print_element_qualifiers): clang's
        // `'CI[2]' (aka 'const int[2]')`, in the const variable's error and an increment's.
        .name = "qualified typedefs of array elements",
        .source = "typedef const int CI;\n"
                  "typedef volatile int VI;\n"
                  "typedef const int CA[3];\n"
                  "void f(void)\n"
                  "{\n"
                  "    CI arr[2] = { 1, 2 };\n"
                  "    arr[0] = 3;\n"
                  "    arr++;\n"
                  "    VI v[4];\n"
                  "    v++;\n"
                  "    CA m[2] = { 0 };\n"
                  "    m[1][2] = 4;\n"
                  "    m++;\n"
                  "}\n",
        .errors = 5,
        .notes = 2,
    },
};

// A variable array's size expression as written (#1447, check_driver_vla_bounds), through pointers, arrays, typedefs
// and conversions, and an anonymous tag's location.
static const CheckDriverDefaultWarningFixture check_driver_vla_bound_fixtures[] =
{
    {
        .name = "variable array bounds and anonymous tags in operand errors",
        .source = "struct { int a; } anon;\n"
                  "union { int b; } anon_u;\n"
                  "void f(int n, int m)\n"
                  "{\n"
                  "    int a[n];\n"
                  "    a++;\n"
                  "    int (*p)[n + 1] = 0;\n"
                  "    p();\n"
                  "    int b[n][m * 2];\n"
                  "    b++;\n"
                  "    char c[sizeof(int) * n];\n"
                  "    c++;\n"
                  "    int d[(n)];\n"
                  "    d();\n"
                  "    anon++;\n"
                  "    anon_u();\n"
                  "    struct { int x; } loc;\n"
                  "    loc();\n"
                  "    enum { E1 } e;\n"
                  "    e();\n"
                  "}\n",
        .errors = 9,
    },
    {
        .name = "variable array bounds of macros, calls, members and typedefs",
        .source = "#define N(x) ((x)*2)\n"
                  "int g(int);\n"
                  "struct S { int k; };\n"
                  "void f(int n, int m, struct S *s, int *ip)\n"
                  "{\n"
                  "    int a[N(n)]; a++;\n"
                  "    int c[g(n) + 1]; c++;\n"
                  "    int d[n ? m : 1]; d++;\n"
                  "    int e[-n + ~m]; e++;\n"
                  "    int h[s->k]; h++;\n"
                  "    int i[*ip]; i++;\n"
                  "    int j[n++]; j++;\n"
                  "    int k[sizeof n]; k++;\n"
                  "    int l[n][3]; l++;\n"
                  "    int o[3][n]; o++;\n"
                  "    typedef int T[n * m];\n"
                  "    T t; t++;\n"
                  "    int p[n << 1 | m]; p++;\n"
                  "    int q[ip[0]]; q++;\n"
                  "    int r[(int)sizeof(struct S) * n]; r++;\n"
                  "    int v[!n + 1]; v++;\n"
                  "}\n",
        .errors = 15,
    },
    {
        .name = "variable array bounds in conversions",
        .source = "void take(double *);\n"
                  "void f(int n)\n"
                  "{\n"
                  "    int a[n];\n"
                  "    int (*p)[n] = &a;\n"
                  "    double *d = p;\n"
                  "    take(&a);\n"
                  "    float x = a;\n"
                  "    int b[n + 1][2];\n"
                  "    struct T { int z; } t = b;\n"
                  "    a = 0;\n"
                  "}\n",
        .errors = 5,
        .notes = 1,
    },
};

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_type_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vla_bound_fixtures, BUSTER_ARRAY_LENGTH(check_driver_vla_bound_fixtures));
}
