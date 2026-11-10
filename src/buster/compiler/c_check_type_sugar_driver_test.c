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
    {
        // Warnings' operands keep their member's typedef (check_common_operand_spelling): -Wsign-compare's, -Wformat's
        // and -Wenum-compare's, through `.`, `->` and a member array's subscript.
        .name = "member typedefs in warning operands",
        .source = "int printf(const char *, ...);\n"
                  "typedef unsigned long ul;\n"
                  "typedef enum { A1, A2 } EA;\n"
                  "typedef enum { B1 } EB;\n"
                  "typedef double D;\n"
                  "struct s { ul n; EA ea; EB eb; D d; ul arr[2]; struct s *next; };\n"
                  "int f(struct s x, struct s *px, int i)\n"
                  "{\n"
                  "    int r = 0;\n"
                  "    r += x.n < i;\n"
                  "    r += px->next->arr[0] < i;\n"
                  "    r += x.ea == x.eb;\n"
                  "    r += x.ea + px->eb;\n"
                  "    r += (x.ea < px->eb);\n"
                  "    printf(\"%d %s %f\\n\", x.n, px->d, x.arr[1]);\n"
                  "    printf(\"%ld\\n\", x.d);\n"
                  "    return r + (i ? x.ea : x.eb);\n"
                  "}\n",
        .options = { "-Wsign-compare" },
        .warnings = 8,
    },
};

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_type_sugar_fixtures));
}
