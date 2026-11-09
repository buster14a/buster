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
        // A target address space one macro writes throughout the unit prints by the macro (check_driver_space_macros,
        // clang's MacroQualifiedType): before its level, with that level's other qualifiers after its specifier; a
        // function-like macro by its invocation, nested macros by the outermost; a typedef's a.k.a. spells it out.
        .name = "address space macro names",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "#define ASN(n) __attribute__((address_space(n)))\n"
                  "#define INNER __attribute__((address_space(4)))\n"
                  "#define OUTER INNER\n"
                  "typedef AS1 int I1;\n"
                  "struct m { AS1 int *f; };\n"
                  "void f(const AS1 int *a, AS1 const volatile int *b, int *AS1 *c, I1 *d, ASN(3) int *e, OUTER int *g, struct m mm, AS1 int *const k, I1 *const *l)\n"
                  "{\n"
                  "    a();\n"
                  "    b();\n"
                  "    c();\n"
                  "    d();\n"
                  "    e();\n"
                  "    g();\n"
                  "    mm.f();\n"
                  "    k();\n"
                  "    l();\n"
                  "    a = e;\n"
                  "    int x = a;\n"
                  "}\n",
        .errors = 11,
    },
    {
        // No macro names a space where clang's FindLocsWithCommonFileID finds none: a predefined macro's (`__seg_gs`, and
        // a user macro over it) and an attribute list that isn't its macro's whole expansion.
        .name = "address spaces no macro names",
        .source = "#define MYGS __seg_gs\n"
                  "#define CAS const __attribute__((address_space(1)))\n"
                  "#define ASV __attribute__((address_space(2))) volatile\n"
                  "#define AS3 __attribute__((address_space(3)))\n"
                  "void f(__seg_gs int *a, MYGS int *b, CAS int *c, ASV int *d, AS3 int *e)\n"
                  "{\n"
                  "    a();\n"
                  "    b();\n"
                  "    c();\n"
                  "    d();\n"
                  "    e();\n"
                  "}\n",
        .errors = 5,
    },
    {
        // Several warnings of one pass print by the macros, each event after the first reading the pass's map
        // (check_driver_spelling_ready builds it below the events' marks).
        .name = "address space macro names in several warnings of one pass",
        .source = "int printf(const char *, ...);\n"
                  "#define AS1 __attribute__((address_space(1)))\n"
                  "#define AS2 __attribute__((address_space(2)))\n"
                  "void f(AS1 int *p, AS2 char *q, AS1 const int *r)\n"
                  "{\n"
                  "    printf(\"%d\\n\", p);\n"
                  "    printf(\"%f\\n\", q);\n"
                  "    printf(\"%d %d\\n\", r, p);\n"
                  "}\n",
        .warnings = 4,
    },
    {
        // The same through incompatible pointer conversions' errors.
        .name = "address space macro names in several conversion errors",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "#define AS2 __attribute__((address_space(2)))\n"
                  "void f(AS1 int *p, AS2 char *q)\n"
                  "{\n"
                  "    AS1 char *a = p;\n"
                  "    AS2 int *b = q;\n"
                  "    AS1 long *c = p;\n"
                  "    AS2 short *d = q;\n"
                  "    (void)a; (void)b; (void)c; (void)d;\n"
                  "}\n",
        .errors = 4,
    },
    {
        // A space written directly with an integer suffix is that space's direct spelling: the macro another declaration
        // writes it with names neither.
        .name = "address spaces written directly with integer suffixes",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "int AS1 *x;\n"
                  "void f(int __attribute__((address_space(1u))) *p, int __attribute__((address_space(1UL))) *r)\n"
                  "{\n"
                  "    int *q = p;\n"
                  "    int *s = r;\n"
                  "}\n",
        .errors = 2,
    },
    {
        // An operand that isn't a plain integer constant (`1+0`) may write any space: no macro names one.
        .name = "address spaces written with a constant expression",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "int AS1 *x;\n"
                  "void f(int __attribute__((address_space(1+0))) *p)\n"
                  "{\n"
                  "    int *q = p;\n"
                  "}\n",
        .errors = 1,
    },
};

// Calling conventions in type spellings (#1447): clang's canonical spelling in an a.k.a. (`intel_ocl_bicc`,
// swiftasynccall without its space), a function designator's preserve_* before it, and a convention one macro writes
// named by the macro before the canonical spelling (check_driver_convention_macros).
static const CheckDriverDefaultWarningFixture check_driver_convention_sugar_fixtures[] =
{
    {
        .name = "calling convention macro names",
        .source = "#define MS __attribute__((ms_abi))\n"
                  "#define VC __attribute__((vectorcall))\n"
                  "#define PM __attribute__((preserve_most))\n"
                  "#define IO __attribute__((intel_ocl_bicc))\n"
                  "MS void a(void);\n"
                  "void b(void) MS;\n"
                  "void VC c(void);\n"
                  "PM void d(void);\n"
                  "IO void e(void);\n"
                  "typedef MS void T(void);\n"
                  "T *t;\n"
                  "void use(void)\n"
                  "{\n"
                  "    int x;\n"
                  "    x = a;\n"
                  "    x = b;\n"
                  "    x = c;\n"
                  "    x = d;\n"
                  "    x = e;\n"
                  "    x = t;\n"
                  "    x = &a;\n"
                  "}\n",
        .errors = 7,
    },
    {
        .name = "calling conventions as written and canonical",
        .source = "typedef void TI(void) __attribute__((intel_ocl_bicc));\n"
                  "typedef void TA(void) __attribute__((swiftasynccall));\n"
                  "void m(void) __attribute__((ms_abi));\n"
                  "void pm(void) __attribute__((preserve_most));\n"
                  "void pa(void) __attribute__((preserve_all));\n"
                  "void pn(void) __attribute__((preserve_none));\n"
                  "void io(void) __attribute__((intel_ocl_bicc));\n"
                  "void __regcall rc(void);\n"
                  "TI *ti;\n"
                  "TA *ta;\n"
                  "void use(void)\n"
                  "{\n"
                  "    int x;\n"
                  "    x = m;\n"
                  "    x = pm;\n"
                  "    x = pa;\n"
                  "    x = pn;\n"
                  "    x = io;\n"
                  "    x = rc;\n"
                  "    x = ti;\n"
                  "    x = ta;\n"
                  "}\n",
        .errors = 8,
    },
};

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_type_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_convention_sugar_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_convention_sugar_fixtures));
}
