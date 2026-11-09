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

// clang's const assignment diagnostics (#1447, check_semantic_const_assignment and check_semantic_const_fields): the
// first const level of a member, subscript and call chain with a note per const level, and a record with const members
// with a note per member, breadth-first.
static const CheckDriverDefaultWarningFixture check_driver_const_assignment_fixtures[] =
{
    {
        // Members const themselves through `.` and `->`, nested chains with a note per level, a const return value, a
        // record with a const member or a nested one, a const pointer member, compound assignments and increments.
        .name = "const members, variables and return values",
        .source = "typedef int I;\n"
                  "struct s { const int a; int b; };\n"
                  "struct in { const int a; int z; };\n"
                  "struct ct { const struct in in; int q; };\n"
                  "struct w { struct ct ct; struct in in2; int k; };\n"
                  "struct p { const int *cp; int *const pc; struct s *ps; };\n"
                  "const struct s *f(void);\n"
                  "const int g(void);\n"
                  "struct t { struct s s; int k; };\n"
                  "void use(void)\n"
                  "{\n"
                  "    struct s x = { 1, 2 }, y = { 3, 4 };\n"
                  "    x.a = 2;\n"
                  "    x.b = 2;\n"
                  "    const struct ct ct = { { 1, 2 }, 3 };\n"
                  "    ct.in.a = 1;\n"
                  "    struct ct m = { { 1, 2 }, 3 };\n"
                  "    m.in.a = 1;\n"
                  "    m.in.z = 1;\n"
                  "    f()->b = 3;\n"
                  "    x = y;\n"
                  "    struct w ww = { 0 }, w2 = { 0 };\n"
                  "    ww = w2;\n"
                  "    ww.ct = w2.ct;\n"
                  "    struct p pp = { 0 };\n"
                  "    *pp.cp = 1;\n"
                  "    pp.pc = 0;\n"
                  "    pp.ps->a = 1;\n"
                  "    x.a++;\n"
                  "    --m.in.a;\n"
                  "    x.a += 1;\n"
                  "    struct t tt = { 0 };\n"
                  "    tt.s = x;\n"
                  "    *f() = x;\n"
                  "    struct s *px = &x;\n"
                  "    *px = y;\n"
                  "    px->a = 4;\n"
                  "    const I ci = 0;\n"
                  "    ci = 1;\n"
                  "}\n",
        .errors = 19,
        .notes = 23,
    },
    {
        // Subscripts of a const pointer, anonymous members, unions, arrays of const and of records.
        .name = "const assignment through subscripts, unions and arrays",
        .source = "typedef const int CI;\n"
                  "typedef int I;\n"
                  "typedef const struct s0 { int v; } CS;\n"
                  "struct an { struct { const int x; int y; }; int z; };\n"
                  "union un { const int a; float f; };\n"
                  "struct arr { const int ca[2]; struct an inner[2]; };\n"
                  "struct deep { struct an an; union un un; CI ci; const I cti; };\n"
                  "static const int *const g(void);\n"
                  "extern __attribute__((unused)) CS *h(void);\n"
                  "typedef float v4f __attribute__((ext_vector_type(4)));\n"
                  "struct vec { const v4f v; };\n"
                  "void use(const int *const p, int i)\n"
                  "{\n"
                  "    p[0] = 1;\n"
                  "    i[p] = 2;\n"
                  "    struct an a = { 0 }, b = { 0 };\n"
                  "    a.x = 1;\n"
                  "    a = b;\n"
                  "    union un u = { 0 }, w = { 0 };\n"
                  "    u = w;\n"
                  "    u.f = 2;\n"
                  "    struct arr r = { 0 }, s = { 0 };\n"
                  "    r.ca[1] = 3;\n"
                  "    r = s;\n"
                  "    r.inner[0].x = 4;\n"
                  "    r.inner[1] = a;\n"
                  "}\n",
        .errors = 9,
        .notes = 9,
    },
    {
        // Typedef'd const members, attributes and storage classes before a const return type, vector elements,
        // parenthesized lvalues and dereferences.
        .name = "const assignment through returns, vectors and parentheses",
        .source = "typedef const int CI;\n"
                  "typedef int I;\n"
                  "typedef const struct s0 { int v; } CS;\n"
                  "struct an { struct { const int x; int y; }; int z; };\n"
                  "union un { const int a; float f; };\n"
                  "struct arr { const int ca[2]; struct an inner[2]; };\n"
                  "struct deep { struct an an; union un un; CI ci; const I cti; };\n"
                  "static const int *const g(void);\n"
                  "extern __attribute__((unused)) CS *h(void);\n"
                  "typedef float v4f __attribute__((ext_vector_type(4)));\n"
                  "struct vec { const v4f v; };\n"
                  "void use(void)\n"
                  "{\n"
                  "    struct an a = { 0 }, b = { 0 };\n"
                  "    struct deep d = { 0 }, e = { 0 };\n"
                  "    d = e;\n"
                  "    d.ci = 1;\n"
                  "    d.cti = 2;\n"
                  "    *g() = 1;\n"
                  "    h()->v = 5;\n"
                  "    struct vec vv = { 0 };\n"
                  "    vv.v.x = 1;\n"
                  "    vv.v[0] = 2;\n"
                  "    a.y += 1;\n"
                  "    (a).x = 1;\n"
                  "    (a) = b;\n"
                  "    (&a)->x = 2;\n"
                  "    *(&a) = b;\n"
                  "}\n",
        .errors = 11,
        .notes = 13,
    },
    {
        // A call's return value is a function's only through a direct callee (clang's getDirectCallee): a function's name
        // through `*`, `&` and parentheses; a function pointer variable's or parameter's call is the generic error.
        .name = "const return values of direct and indirect calls",
        .source = "struct s { int a; const int b; };\n"
                  "const struct s *f(void);\n"
                  "const struct s *(*hp)(void);\n"
                  "void use(const struct s *(*lp)(void))\n"
                  "{\n"
                  "    f()->a = 1;\n"
                  "    hp()->a = 2;\n"
                  "    lp()->a = 3;\n"
                  "    (*hp)()->a = 4;\n"
                  "    hp()->b = 5;\n"
                  "    (&f)()->a = 6;\n"
                  "    (*f)()->a = 7;\n"
                  "    (**f)()->a = 8;\n"
                  "    (&*f)()->a = 9;\n"
                  "    ((f))()->a = 10;\n"
                  "}\n",
        .errors = 10,
        .notes = 7,
    },
};

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_type_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_const_assignment_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_const_assignment_fixtures));
}
