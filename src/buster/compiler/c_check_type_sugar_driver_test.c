// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Type sugar in diagnostics (#1447): original fixtures whose whole diagnostic output is compared with clang's on both
// paths, at each warning policy, with -fsyntax-only and -c, on one and three lanes. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static const CheckDriverDefaultWarningFixture check_driver_type_sugar_fixtures[] =
{
    {
        .name = "aligned typedefs retain their names in conversions and incomplete arrays",
        .source = "typedef char C __attribute__((aligned(8)));\nC *p = 1;\nC c = \"x\";\n"
                  "struct X;\ntypedef struct X TX __attribute__((aligned(8)));\nTX a[3];\n",
        .errors = 4,
        .notes = 1,
    },
    {
        .name = "an aligned typedef names an array element whose size is not a multiple of its alignment",
        .source = "typedef char C __attribute__((aligned(8)));\nC a[2];\n",
        .errors = 1,
    },
    {
        .name = "aligned typedefs retain their own and outer qualifiers without naming plain neighbors",
        .source = "typedef const int CI __attribute__((aligned(8)));\ntypedef int *P __attribute__((aligned(32)));\n"
                  "void f(void){volatile CI *a=1;const P b=1;P c=1;char *d=1;}\n",
        .errors = 4,
    },
    {
        .name = "a qualified aligned typedef names its incomplete array element",
        .source = "struct X;\ntypedef struct X TX __attribute__((aligned(8)));\nconst TX a[3];\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a decayed function typedef survives pointer arithmetic at file scope",
        .source = "typedef int F(void);\nF f1;\nint z2 = f1 + 1;\n",
        .errors = 2,
    },
    {
        .name = "function typedefs survive both addition orders subtraction and address operations",
        .source = "typedef int F(void);typedef F G;\nG g;F f;\nvoid h(void){int a=f+1;int b=1+f;int c=f-1;"
                  "int d=g+2;int e=&f;int z=*&f+3;int (*raw)(void)=f;int w=raw+1;}\n",
        .errors = 7,
    },
    {
        .name = "function pointer typedefs in members parameters and casts keep their spelling",
        .source = "typedef int F(void);typedef F *FP;\nstruct S{F *p;FP q;};\n"
                  "void h(struct S s,FP p){int a=s.p+1;int b=p+1;int c=(F*)0+1;int d=*(F*)0+1;}\n",
        .errors = 4,
    },
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
        // A qualifier written again beside a qualified typedef (check_input_typedef_sugar) prints over its name, as
        // clang's qualifiers over a typedef's sugar do (`'const CI[1]' (aka 'const int[1]')`); a typedef of fewer
        // qualifiers than its level's names it under the others (`'volatile CI[2]'`).
        .name = "qualifiers written again over a qualified typedef",
        .source = "typedef const int CI;\n"
                  "typedef volatile int VI;\n"
                  "typedef unsigned U;\n"
                  "struct s { const CI m; CI n; };\n"
                  "void f(const CI y, volatile U u, struct s *ps)\n"
                  "{\n"
                  "    const CI twice[1] = { 1 };\n"
                  "    twice++;\n"
                  "    const CI one = 1;\n"
                  "    one = 2;\n"
                  "    CI const after[2] = { 1 };\n"
                  "    after++;\n"
                  "    volatile CI mixed[2] = { 0 };\n"
                  "    mixed++;\n"
                  "    const VI cv[2] = { 0 };\n"
                  "    cv++;\n"
                  "    static const CI s2[2][3];\n"
                  "    s2++;\n"
                  "    const CI *p = 0;\n"
                  "    p();\n"
                  "    if (y < u) {}\n"
                  "    if (ps->m < u) {}\n"
                  "    ps->m();\n"
                  "    ps->n();\n"
                  "    const U a = 1, b = 2;\n"
                  "    if (a + b < y) {}\n"
                  "}\n",
        .options = { "-Wsign-compare" },
        .warnings = 3,
        .errors = 9,
        .notes = 1,
    },
    {
        // A format argument prints its type as clang's lvalue conversion leaves it (check_common_format_value): without
        // its top-level qualifiers, so a typedef that holds them itself prints desugared (`CI`'s `'int'`), and an array
        // decays to a pointer to its qualified element (`'const int *'`).
        .name = "format arguments' value types",
        .source = "int printf(const char *, ...);\n"
                  "typedef const int CI;\n"
                  "typedef int I;\n"
                  "typedef const char *CS;\n"
                  "const int g = 1;\n"
                  "const I gi = 2;\n"
                  "CI gc = 3;\n"
                  "struct s { const I m; CI n; const int k; };\n"
                  "void f(const int l, const I x, CI y, struct s t, const CS q, const I *pi, volatile I vi)\n"
                  "{\n"
                  "    printf(\"%s %s %s %s %s\\n\", g, gi, gc, l, x);\n"
                  "    printf(\"%s %s %s %s %d %d\\n\", y, t.m, t.n, t.k, q, pi);\n"
                  "    printf(\"%s %s\\n\", vi, *pi);\n"
                  "    const int a[2] = { 0 };\n"
                  "    printf(\"%d\\n\", a);\n"
                  "}\n",
        .warnings = 14,
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
        // Each declaration's own address space macro (#1447 item 2, check_driver_space_scope): two macros of one space and a
        // direct spelling, in the address space errors' operands (an assignment's, an initialization's, a return's, a call's,
        // a nested pointer's, and both operands of non-overlapping spaces' arithmetic); a typedef's by the unit's.
        .name = "address space macros per declaration",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "#define ASA __attribute__((address_space(1)))\n"
                  "#define AS2 __attribute__((address_space(2)))\n"
                  "typedef AS1 int T1;\n"
                  "void g(AS2 int *q);\n"
                  "AS1 int *ret(ASA int *x) { return x; }\n"
                  "void f(AS1 int *pa, ASA int *pb)\n"
                  "{\n"
                  "    AS1 int *a = 0;\n"
                  "    ASA int *b = 0;\n"
                  "    __attribute__((address_space(1))) int *c = 0;\n"
                  "    AS2 int *d = 0;\n"
                  "    T1 *t = 0;\n"
                  "    long *p;\n"
                  "    int *i = a;\n"
                  "    AS2 int *e = b;\n"
                  "    p = a;\n"
                  "    p = b;\n"
                  "    p = c;\n"
                  "    p = t;\n"
                  "    p = pa;\n"
                  "    p = pb;\n"
                  "    d = a;\n"
                  "    a = d;\n"
                  "    b = d;\n"
                  "    g(a);\n"
                  "    g(b);\n"
                  "    p = &*b;\n"
                  "    long *q = pb;\n"
                  "    int x = d - b;\n"
                  "    AS1 int **pp = 0;\n"
                  "    int **ip = pp;\n"
                  "}\n",
        .errors = 17,
        .notes = 2,
    },
    {
        .name = "address space macros of one declaration",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "#define ASA __attribute__((address_space(1)))\n"
                  "void f(void)\n"
                  "{\n"
                  "    AS1 int *a = 0;\n"
                  "    ASA int *b = 0;\n"
                  "    __attribute__((address_space(1))) int *c = 0;\n"
                  "    long *p;\n"
                  "    p = a;\n"
                  "    p = b;\n"
                  "    p = c;\n"
                  "    p = &*a;\n"
                  "}\n",
        .errors = 4,
    },
    {
        // A failed conversion's types, by each declaration's macro, and no "dereference with *" where the pointee's space
        // differs from the target's (compareTypesSimple's compatiblyIncludes).
        .name = "address space macros in conversions",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "#define ASA __attribute__((address_space(1)))\n"
                  "void h(ASA long *z);\n"
                  "void f(void)\n"
                  "{\n"
                  "    AS1 int *a = 0;\n"
                  "    ASA long *l = a;\n"
                  "    AS1 long *m = 0;\n"
                  "    m = a;\n"
                  "    l = a;\n"
                  "    h(a);\n"
                  "    int n = a;\n"
                  "    ASA char c = 0;\n"
                  "    char *pc = &c;\n"
                  "}\n",
        .errors = 6,
        .notes = 1,
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
    {
        // A multidimensional array's qualifiers pass through each element array to the typedef that names them.
        .name = "qualified typedefs of multidimensional array elements",
        .source = "typedef const int CI;\n"
                  "typedef volatile int VI;\n"
                  "void f(void) { CI b[2][3] = { 0 }; b++; b[0]++; VI c[1][2][3]; c++; c[0]++; }\n",
        .errors = 4,
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
    {
        // clang's desugarForDiagnostic doesn't look inside an _Atomic (check_driver_atomic_value): a typedef that is an
        // atomic's value type gives no a.k.a.
        .name = "typedefs inside _Atomic",
        .source = "typedef int I;\n"
                  "typedef long L;\n"
                  "struct S { int x; };\n"
                  "typedef struct S TS;\n"
                  "void f(_Atomic(I) *ap, _Atomic(L) a2[2], _Atomic(TS) *sp, _Atomic I *kw)\n"
                  "{\n"
                  "    ap();\n"
                  "    char *d = ap;\n"
                  "    char *e = a2;\n"
                  "    char *g = sp;\n"
                  "    char *h = kw;\n"
                  "}\n",
        .errors = 5,
    },
    {
        // A typeof's operand prints as written (#1837, check_driver_typeof_text): an expression as clang's StmtPrinter
        // does, with expression spacing (`'typeof (x + 1) *'`), and a type name with its typedef names (`'typeof(L *)'`).
        .name = "typeof expressions and type names with typedefs",
        .source = "typedef long L;\n"
                  "typedef int I;\n"
                  "int x, arr[4];\n"
                  "struct T { int f; } t;\n"
                  "typeof(x + 1) y;\n"
                  "typeof(L *) z;\n"
                  "typeof(x+1) *w;\n"
                  "typeof(arr[1]) v;\n"
                  "typeof(t.f) u;\n"
                  "typeof(-x) n;\n"
                  "typeof((x)) pq;\n"
                  "typeof(sizeof x) sz;\n"
                  "typeof(L const *) cz;\n"
                  "typeof(I *) iz[2];\n"
                  "typeof(x ? 1 : 2) cond;\n"
                  "void f(void)\n"
                  "{\n"
                  "    char *a = &y;\n"
                  "    char *b = z;\n"
                  "    char *c = w;\n"
                  "    char *d = &v;\n"
                  "    char *e = &u;\n"
                  "    char *g = &n;\n"
                  "    char *h = &pq;\n"
                  "    char *i = &sz;\n"
                  "    char *j = cz;\n"
                  "    char *k = iz;\n"
                  "    char *l = &cond;\n"
                  "}\n",
        .errors = 11,
    },
    {
        // The same in C23, with GNU's spellings, typeof_unqual, a nested typeof, a call, a cast and a string literal.
        .name = "typeof expressions in C23",
        .source = "typedef long L;\n"
                  "int x;\n"
                  "const int cx = 0;\n"
                  "__typeof__(x * 2) a;\n"
                  "__typeof(x) b2;\n"
                  "typeof_unqual(cx + 1) c;\n"
                  "typeof_unqual(L *) d;\n"
                  "typeof(typeof(x + 1)) e;\n"
                  "int f(int);\n"
                  "typeof(f(x)) h;\n"
                  "typeof((L)x) i;\n"
                  "typeof(\"ab\") s;\n"
                  "void k(void)\n"
                  "{\n"
                  "    char *p1 = &a; char *p3 = &c; char *p4 = d; char *p5 = &e; char *p6 = &h; char *p7 = &i; int *p8 = &s;\n"
                  "}\n",
        .errors = 7,
        .c23 = true,
    },
    {
        // A member's own declarator's specifiers spell its type (#1837, check_semantic_member_token): a conversion's
        // source through `&` and `*` (check_driver_pointer_spelling), an assignment's member target, and the null pointer
        // warning's.
        .name = "member typedefs and typeofs in conversions",
        .source = "typedef long L;\n"
                  "struct S { typeof(long) m; L l; typeof(L *) p; } s;\n"
                  "int x;\n"
                  "typeof(x + 1) y;\n"
                  "typeof(L *) z;\n"
                  "void f(void)\n"
                  "{\n"
                  "    char *a = &s.m;\n"
                  "    char *b = &s.l;\n"
                  "    char *c = s.p;\n"
                  "    char *d = &y;\n"
                  "    char *e = z;\n"
                  "}\n",
        .errors = 5,
    },
    {
        .name = "member typedefs as conversion sources and targets",
        .source = "typedef long L;\n"
                  "typedef L *LP;\n"
                  "typedef int I;\n"
                  "struct In { L deep; };\n"
                  "struct S { L l; LP lp; LP *lpp; struct In in; L arr[3]; typeof(I) ti; I *ip; const L cl; } s, *ps;\n"
                  "void take(char *);\n"
                  "void f(void)\n"
                  "{\n"
                  "    char *a = &ps->l;\n"
                  "    char *b = s.lp;\n"
                  "    char *c = *s.lpp;\n"
                  "    char *d = &s.in.deep;\n"
                  "    char *e = s.arr;\n"
                  "    char *g = &s.arr[1];\n"
                  "    char *h = &s.ti;\n"
                  "    char *k = &s.cl;\n"
                  "    take(&s.l);\n"
                  "    take(s.lp);\n"
                  "    char *m;\n"
                  "    m = s.ip;\n"
                  "    s.lp = m;\n"
                  "    s.ip = m;\n"
                  "    ps->lp = m;\n"
                  "    s.l = m;\n"
                  "    return;\n"
                  "}\n",
        .errors = 15,
        .notes = 2,
    },
    {
        .name = "member typedefs in null pointer warnings",
        .source = "typedef long *LP;\n"
                  "typedef unsigned U;\n"
                  "struct S { LP lp; U u; } s, *ps;\n"
                  "int n;\n"
                  "void f(void)\n"
                  "{\n"
                  "    s.lp = 1-1;\n"
                  "    ps->lp = 1-1;\n"
                  "    (s).lp = 1-1;\n"
                  "    int r = s.u < n;\n"
                  "    static int z = sizeof(s.u < -1);\n"
                  "    LP local = 1-1;\n"
                  "}\n",
        .options = { "-Wsign-compare" },
        .warnings = 5,
    },
    {
        // A compound literal's type name spells its value's type (#1812, check_input_token_type_spelling): through `&`, a
        // decay, a member, -Wformat's and -Wsign-compare's operands.
        .name = "compound literals' typedefs",
        .source = "int printf(const char *, ...);\n"
                  "typedef long L;\n"
                  "typedef unsigned U;\n"
                  "typedef struct P { int x; } P;\n"
                  "typedef int A2[2];\n"
                  "void f(int n)\n"
                  "{\n"
                  "    char *a = &(L){ 1 };\n"
                  "    char *b = (A2){ 1, 2 };\n"
                  "    int c = (P){ 1 };\n"
                  "    char *d = &(const L){ 2 };\n"
                  "    printf(\"%s %s\\n\", (L){ 3 }, (A2){ 0 });\n"
                  "    if (n < (U){ 4 }) {}\n"
                  "    char *e = (L[]){ 1, 2, 3 };\n"
                  "    char *g = &(P){ 0 }.x;\n"
                  "}\n",
        .options = { "-Wsign-compare" },
        .warnings = 3,
        .errors = 6,
    },
    {
        // GNU's array initialized by a compound literal of another array type: the literal's typedef as written.
        .name = "an array literal's typedef in its mismatch",
        .source = "typedef int A2[2];\n"
                  "typedef const int CA[2];\n"
                  "void f(void)\n"
                  "{\n"
                  "    int x[3] = (A2){1,2};\n"
                  "    long y[2] = (A2){1,2};\n"
                  "    int z[2] = (CA){1,2};\n"
                  "}\n",
        .errors = 3,
    },
    {
        // A null pointer constant argument's warning prints its parameter's type with the typedef the callee's declaration
        // writes (#1100, check_input_parameter_spelling): a function's, a function pointer object's (through `*`), its
        // typedef'd function type's and a member's own parameter list; a variadic argument has none.
        .name = "call arguments' parameter typedefs in null pointer warnings",
        .source = "typedef struct N { int x; } N;\n"
                  "typedef N *NP;\n"
                  "typedef int *IP;\n"
                  "typedef void F(N *);\n"
                  "void a(const N *q);\n"
                  "void b(NP);\n"
                  "void h(int x, IP p, N *const r);\n"
                  "void e(N *q[]);\n"
                  "void f(int n, N *(*g)(void), NP const r);\n"
                  "void d(int, ...);\n"
                  "void (*fp)(N *);\n"
                  "F *gp;\n"
                  "struct S { void (*m)(N *); } s;\n"
                  "static int z = sizeof(a(1-1), 0);\n"
                  "int main(void)\n"
                  "{\n"
                  "    a(1-1); b(1-1); h(0, 1-1, 1-1); e(1-1); f(0, 1-1, 1-1); d(1, 1-1);\n"
                  "    (a)(1-1); fp(1-1); (*fp)(1-1); gp(1-1); s.m(1-1);\n"
                  "    void (*lp)(int, N *) = 0;\n"
                  "    lp(0, 1-1);\n"
                  "    return 0;\n"
                  "}\n",
        .warnings = 13,
    },
    {
        // A conversion's target keeps the typedef its declaration writes (#1447): an enumeration conversion's parameter
        // (check_input_call_parameter_spelling), and an initializer subobject's member or element, found by its offset
        // from the initialized object (check_semantic_subobject_spelling), in -Wimplicit-enum-enum-cast and in a failed
        // conversion's error alike.
        .name = "parameter, member and element targets' typedefs",
        .source = "enum E { A, B };\n"
                  "enum F { X, Y };\n"
                  "typedef enum E TE;\n"
                  "typedef long L;\n"
                  "typedef long *LP;\n"
                  "struct S { TE te; L l; LP lp; };\n"
                  "void take(TE e);\n"
                  "TE arr[2];\n"
                  "void f(enum F v, char *c)\n"
                  "{\n"
                  "    take(X);\n"
                  "    take(v);\n"
                  "    struct S s = { .te = X };\n"
                  "    struct S t = { v };\n"
                  "    s.te = v;\n"
                  "    arr[0] = v;\n"
                  "    TE local = v;\n"
                  "    TE ls[2] = { v, X };\n"
                  "    struct S w = { .lp = c };\n"
                  "    struct S z = { 0, c };\n"
                  "    L la[2] = { c };\n"
                  "    LP lpa[2] = { 0, c };\n"
                  "}\n"
                  "struct In { TE e; LP p; };\n"
                  "struct Out { int k; struct In in[2]; union { L ul; TE ue; } u; };\n"
                  "void g(enum F v, char *c, struct S *ps)\n"
                  "{\n"
                  "    struct Out o = { 0, { { X, 0 }, { .e = v } }, { .ue = v } };\n"
                  "    struct Out q = { 0, { { A, 0 }, { .p = c } } };\n"
                  "    void (*fp)(int, TE) = 0;\n"
                  "    fp(0, v);\n"
                  "    (*fp)(1, X);\n"
                  "}\n",
        .warnings = 14,
        .errors = 5,
    },
    {
        // A discarded call whose callee has no function's name keeps its return type's typedef (#1447,
        // check_common_ignored_callee): a member's, an element's, a call's result's, a comma's and a conditional's value
        // and a pointer to a pointer's dereference, each by the typedef past its declarator's `*`.
        .name = "nodiscard return typedefs through nameless callees",
        .source = "typedef int R __attribute__((warn_unused_result));\n"
                  "struct __attribute__((warn_unused_result)) S { int x; };\n"
                  "typedef R (*FP)(void);\n"
                  "struct T { R (*m)(void); struct S (*sm)(void); } t, *pt;\n"
                  "R (*arr[2])(void);\n"
                  "FP get(void);\n"
                  "void f(int n, R (**pp)(void), FP a, FP b, struct S (*sp)(void))\n"
                  "{\n"
                  "    t.m();\n"
                  "    pt->sm();\n"
                  "    arr[0]();\n"
                  "    get()();\n"
                  "    (n, a)();\n"
                  "    (n ? a : b)();\n"
                  "    (*pp)();\n"
                  "    sp();\n"
                  "    a();\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 10,
    },
    {
        // The same callees whose pointer returns another type: no warning.
        .name = "nameless callees of unmarked return types",
        .source = "typedef int R __attribute__((warn_unused_result));\n"
                  "typedef int Q;\n"
                  "struct T { Q (*m)(void); R *(*rp)(void); } t;\n"
                  "Q (*arr[2])(void);\n"
                  "R *(*parr[2])(void);\n"
                  "Q (*get(void))(void);\n"
                  "void f(int n, Q (**pp)(void), R *(**rpp)(void))\n"
                  "{\n"
                  "    t.m(); t.rp(); arr[0](); parr[1](); get()(); (*pp)(); (*rpp)(); (n, arr[0])();\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 1,
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
    {
        // A cast's builtin type keywords print as clang's TypePrinter spells the type (check_semantic_builtin_spelling):
        // `(unsigned)n` is `(unsigned int)n`, `(long unsigned)n` `(unsigned long)n`, `(signed)1` `(int)1`.
        .name = "cast type names in variable array bounds",
        .source = "typedef long L;\n"
                  "int printf(const char *, ...);\n"
                  "int n;\n"
                  "void f(void)\n"
                  "{\n"
                  "    int a[(unsigned)n];\n"
                  "    int b[(long unsigned)n];\n"
                  "    int c[(const short)n + (signed)1];\n"
                  "    int d[(unsigned long long int)n];\n"
                  "    int e[(L)n * (char)2];\n"
                  "    int g[(int long)n];\n"
                  "    int h[(unsigned char)n];\n"
                  "    int i[(signed char)n + (short int)n];\n"
                  "    a(); b(); c(); d(); e(); g(); h(); i();\n"
                  "}\n",
        .errors = 8,
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
    {
        // A declaration's convention as its type sugar keeps it (c_check_type_sugar_input.c): clang's AttributedType where
        // the attribute is written, inside the ParenType of a pointer's group (`'__attribute__((preserve_most))  void
        // ((*))(void)'`), its macro's MacroQualifiedType, the attribute a macro in a group before `*` writes dropped with
        // its function's modified type (`void (MS *r)(void)`'s `'MS void (*)(void)'`), a pointer's own, and the ones
        // reached through `*` and `&`.
        .name = "declarator convention sugar",
        .source = "#define MS __attribute__((ms_abi))\n"
                  "void f(void) __attribute__((preserve_most));\n"
                  "void (*p)(void) __attribute__((preserve_most));\n"
                  "void (**pp)(void) __attribute__((preserve_all));\n"
                  "void (*a[2])(void) __attribute__((preserve_none));\n"
                  "typedef void F(void) __attribute__((preserve_most));\n"
                  "F *fp;\n"
                  "void (*q)(void) __attribute__((ms_abi));\n"
                  "void (MS *r)(void);\n"
                  "void (* MS s)(void);\n"
                  "void (MS **t)(void);\n"
                  "MS void (*u)(void);\n"
                  "void (*v)(void) MS;\n"
                  "void g(void) __attribute__((sysv_abi));\n"
                  "void (*w)(void) __attribute__((sysv_abi));\n"
                  "void h(void)\n"
                  "{\n"
                  "    int x1 = f, x2 = p, x3 = pp, x4 = a, x5 = fp, x6 = q, x7 = r, x8 = s;\n"
                  "    int x9 = t, x10 = u, x11 = v, x12 = &f, x13 = g, x14 = w, x15 = &g, x16 = *pp;\n"
                  "}\n",
        .warnings = 1,
        .errors = 16,
    },
    {
        // Every placement: a const pointer's group and its own `* const`, a pointer to pointer's inner `*`, arrays, a
        // function returning a pointer, a function-like macro, a group's sysv_abi, specifiers', C23 lists before the
        // specifiers (the whole type's) and after the declarator, a local's, and assignments' targets.
        .name = "convention sugar placements",
        .source = "#define MS __attribute__((ms_abi))\n"
                  "#define PM __attribute__((preserve_most))\n"
                  "#define AS2(x) __attribute__((x))\n"
                  "void (*const c1)(void) MS = 0;\n"
                  "void (* const MS c2)(void) = 0;\n"
                  "void (* MS *pq)(void);\n"
                  "void (*arr[3])(void) PM;\n"
                  "void (*(*fpf)(int))(void) MS;\n"
                  "void (AS2(ms_abi) *m2)(void);\n"
                  "void (__attribute__((sysv_abi)) *sg)(void);\n"
                  "__attribute__((vectorcall)) void (*vc)(void);\n"
                  "[[gnu::ms_abi]] void (*c23)(void);\n"
                  "void (*c23b)(void) [[gnu::ms_abi]];\n"
                  "void h(void)\n"
                  "{\n"
                  "    void (*local)(void) MS;\n"
                  "    int a = local, c = pq, d = *pq, e = arr, f = arr[1], g = &arr, i = fpf, k = m2, l = sg;\n"
                  "    int m = vc, n = c23, o = c23b, r = c1, s = c2;\n"
                  "    local = 1;\n"
                  "    pq = 2;\n"
                  "    *pq = 3;\n"
                  "}\n",
        .warnings = 1,
        .errors = 17,
    },
    {
        // Each convention's AttributedType spelling, explicit sysv_abi and cdecl too, which the canonical type doesn't
        // keep; and no_caller_saved_registers beside one, with which clang rebuilds the type without its sugar.
        .name = "convention attribute spellings",
        .source = "void a1(void) __attribute__((sysv_abi));\n"
                  "void a2(void) __attribute__((ms_abi));\n"
                  "void a3(void) __attribute__((vectorcall));\n"
                  "void a4(void) __attribute__((regcall));\n"
                  "void a5(void) __attribute__((preserve_all));\n"
                  "void a8(void) __attribute__((no_caller_saved_registers));\n"
                  "void a9(void) __attribute__((intel_ocl_bicc));\n"
                  "void a10(void) __attribute__((swiftcall));\n"
                  "void a11(void) __attribute__((swiftasynccall));\n"
                  "void __regcall a12(void);\n"
                  "void __vectorcall a13(void);\n"
                  "void a15(void) __attribute__((__ms_abi__));\n"
                  "void a16(void) __attribute__((ms_abi, no_caller_saved_registers));\n"
                  "void a17(void) __attribute__((cdecl));\n"
                  "void (*b9)(void) __attribute__((intel_ocl_bicc));\n"
                  "void (__regcall *b12)(void);\n"
                  "void (* __attribute__((vectorcall)) b3)(void);\n"
                  "void h(void)\n"
                  "{\n"
                  "    int x1 = a1, x2 = a2, x3 = a3, x4 = a4, x5 = a5, x8 = a8, x9 = a9, x10 = a10, x11 = a11;\n"
                  "    int x12 = a12, x13 = a13, x15 = a15, x16 = a16, x17 = a17, y9 = b9, y12 = b12, y3 = b3;\n"
                  "}\n",
        .errors = 17,
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

// size_t's __size_t sugar (#1447 item 10, check_driver_size_sugar and check_driver_library_designator): an implicitly
// declared library builtin's designator and address by its prototype, its call's value, and arithmetic as clang's usual
// arithmetic conversions keep the sugar of the operand the result takes its type from.
static const CheckDriverDefaultWarningFixture check_driver_size_sugar_fixtures[] =
{
    {
        .name = "library builtins' __size_t",
        .source = "typedef struct _IO_FILE FILE;\n"
                  "void f(const char *s, char *d, unsigned long n, int i)\n"
                  "{\n"
                  "    int *p = strlen(s);\n"
                  "    void *m = malloc(1);\n"
                  "    memcpy(d, s, 1);\n"
                  "    int *q = malloc;\n"
                  "    int *r = memcpy;\n"
                  "    int *t = strlen;\n"
                  "    int *u = &strlen;\n"
                  "    char *a = strlen(s) + 1;\n"
                  "    char *b = sizeof(int) * strlen(s);\n"
                  "    char *c = (strlen(s));\n"
                  "    char *e = sizeof(int) + 1;\n"
                  "}\n",
        .errors = 12,
        .notes = 3,
    },
    {
        .name = "__size_t through arithmetic",
        .source = "typedef struct _IO_FILE FILE;\n"
                  "void f(const char *s, char *d, unsigned long n, int i)\n"
                  "{\n"
                  "    char *e = sizeof(int) + 1;\n"
                  "    char *g = 1 + sizeof(int);\n"
                  "    char *h = n + sizeof(int);\n"
                  "    char *k = sizeof(int) + n;\n"
                  "    char *l = i ? sizeof(int) : 2;\n"
                  "    char *o = -sizeof(int);\n"
                  "    char *v = sizeof(int) << 2;\n"
                  "    char *w = strlen(s) - n;\n"
                  "    char *x = (unsigned long)strlen(s);\n"
                  "    char *y = i + strlen(s);\n"
                  "}\n",
        .errors = 11,
        .notes = 1,
    },
};

// A failed static assertion's details (#2171, check_semantic_assert): clang's "expression evaluates to" note, the failing
// conjunct of an `&&`, and its requirement's number literals as clang's StmtPrinter prints them.
static const CheckDriverDefaultWarningFixture check_driver_static_assert_fixtures[] =
{
    {
        // A failed assertion of a binary operator but `||` notes its sides' values at the operator (clang's
        // DiagnoseStaticAssertDetails), where a side isn't a literal: casts, sizeof, arithmetic, enumeration constants.
        .name = "comparisons' sides evaluated",
        .source = "_Static_assert(1 == 2, \"a\");\n"
                  "_Static_assert((unsigned)1 == 2, \"x\");\n"
                  "_Static_assert((long unsigned)sizeof(int) == 3, \"y\");\n"
                  "_Static_assert(sizeof(int) == 3, \"z\");\n"
                  "_Static_assert(1 + 1 == 3, \"w\");\n"
                  "_Static_assert((char)1 < 0, \"v\");\n"
                  "enum { K = 2 };\n"
                  "_Static_assert(K == 3, \"u\");\n"
                  "_Static_assert((int)K == 3, \"t\");\n",
        .errors = 8,
        .notes = 7,
    },
    {
        // An `&&`'s first failing conjunct is the requirement, at it (findFailedBooleanCondition); integer literals print
        // by value and suffix (StmtPrinter's `1U`, `9223372036854775807LL`); a negative side, a char, a bool.
        .name = "failing conjuncts and literal spellings",
        .source = "enum { K = 2, Z = 0 };\n"
                  "_Static_assert(K - 2, \"a\");\n"
                  "_Static_assert(K && Z, \"b\");\n"
                  "_Static_assert(Z || Z, \"c\");\n"
                  "_Static_assert(K == -3, \"d\");\n"
                  "_Static_assert(-K == 3, \"e\");\n"
                  "_Static_assert(1.5 == K, \"f\");\n"
                  "_Static_assert((_Bool)K == 0, \"g\");\n"
                  "_Static_assert((K) == (3), \"h\");\n"
                  "_Static_assert(K & 1, \"i\");\n"
                  "_Static_assert('a' == K, \"j\");\n"
                  "_Static_assert((signed char)-1 == K, \"k\");\n"
                  "_Static_assert(K == 3 && 1, \"l\");\n"
                  "_Static_assert(!K, \"m\");\n"
                  "_Static_assert(K < 1u, \"n\");\n"
                  "_Static_assert(0x7fffffffffffffffLL == K, \"o\");\n"
                  "_Static_assert((unsigned char)200 == K, \"p\");\n"
                  "_Static_assert((char)'\\n' == K, \"q\");\n",
        .errors = 17,
        .notes = 14,
    },
    {
        // A plain char's value as a character literal with its code unit and value (escapes, `<U+0000>`, `<0xFF>`), a
        // bool's `false`, an unsigned long long's and an __int128's values.
        .name = "character and integer values",
        .source = "enum { K = 2 };\n"
                  "_Static_assert((char)-1 == K, \"\");\n"
                  "_Static_assert((char)'a' == K, \"\");\n"
                  "_Static_assert((char)' ' == K, \"\");\n"
                  "_Static_assert((char)'\\'' == K, \"\");\n"
                  "_Static_assert((char)'\\\\' == K, \"\");\n"
                  "_Static_assert((char)127 == K, \"\");\n"
                  "_Static_assert((char)'\\t' == K, \"\");\n"
                  "_Static_assert((char)'\"' == K, \"\");\n"
                  "_Static_assert((char)0 == K, \"\");\n"
                  "_Static_assert((_Bool)0 == K, \"\");\n"
                  "_Static_assert((unsigned long long)-1 == K, \"\");\n"
                  "_Static_assert(K == (__int128)3, \"\");\n"
                  "_Static_assert((char)200 == K, \"\");\n",
        .errors = 13,
        .notes = 13,
    },
    {
        // The same with -funsigned-char: a char past 0x7F is positive.
        .name = "character values of unsigned char",
        .source = "enum { K = 2 };\n"
                  "_Static_assert((char)-1 == K, \"\");\n"
                  "_Static_assert((char)'a' == K, \"\");\n"
                  "_Static_assert((char)' ' == K, \"\");\n"
                  "_Static_assert((char)'\\'' == K, \"\");\n"
                  "_Static_assert((char)'\\\\' == K, \"\");\n"
                  "_Static_assert((char)127 == K, \"\");\n"
                  "_Static_assert((char)'\\t' == K, \"\");\n"
                  "_Static_assert((char)'\"' == K, \"\");\n"
                  "_Static_assert((char)0 == K, \"\");\n"
                  "_Static_assert((_Bool)0 == K, \"\");\n"
                  "_Static_assert((unsigned long long)-1 == K, \"\");\n"
                  "_Static_assert(K == (__int128)3, \"\");\n"
                  "_Static_assert((char)200 == K, \"\");\n",
        .options = { "-funsigned-char" },
        .errors = 13,
        .notes = 13,
    },
    {
        // Floating sides and literals as llvm's APFloat::toString spells them: `100`, `0.5`, `0.10000000000000001`,
        // `1.0E+10`; a literal's trailing `.` and its `F` (`2.`, `1.5F`).
        .name = "floating values and literals",
        .source = "enum{K=2};\n"
                  "_Static_assert(2.0 == K+1, \"\");\n"
                  "_Static_assert(0.5 == K, \"\");\n"
                  "_Static_assert(100.0 == K, \"\");\n"
                  "_Static_assert(1e10 == K, \"\");\n"
                  "_Static_assert(0.1 == K, \"\");\n"
                  "_Static_assert(1.0/3 == K, \"\");\n"
                  "_Static_assert(1.5f == K, \"\");\n"
                  "_Static_assert(1234567.0 == K, \"\");\n"
                  "_Static_assert(-0.25 == K, \"\");\n"
                  "_Static_assert(1e-5 == K, \"\");\n"
                  "_Static_assert(1000.0 == K, \"\");\n"
                  "_Static_assert(10000.0 == K, \"\");\n",
        .errors = 12,
        .notes = 12,
    },
    {
        // C23's static_assert without a message, `nullptr`, and no note for a boolean value compared with `true` or
        // `false` (isKnownToHaveBooleanValue); `(bool)` casts.
        .name = "C23 assertions and boolean literals",
        .source = "enum { K = 2 };\n"
                  "static_assert(K == 3);\n"
                  "static_assert(true == (K == 3));\n"
                  "static_assert((K == 3) == true);\n"
                  "static_assert((K == 2) && (K == 3) && 1, \"x\");\n"
                  "static_assert(((K == 2) && (1 && K > 5)), \"y\");\n"
                  "static_assert(nullptr != nullptr, \"z\");\n"
                  "static_assert(K + 0u == 3, \"w\");\n"
                  "static_assert((K == 2) && !K, \"v\");\n"
                  "static_assert(sizeof(long double) == 4);\n"
                  "static_assert(1 ? K == 3 : 0);\n",
        .errors = 10,
        .notes = 5,
        .c23 = true,
    },
    {
        // A boolean literal beside a value that isn't known to be boolean is noted.
        .name = "C23 boolean literal sides",
        .source = "enum { K = 2 };\n"
                  "static_assert(true == K);\n"
                  "static_assert(K == false + 3);\n"
                  "static_assert(false == (K + 1));\n"
                  "static_assert((_Bool)K == false);\n"
                  "static_assert(false != 0);\n",
        .errors = 5,
        .notes = 3,
        .c23 = true,
    },
    {
        // A static assertion among a structure's or a union's members (#2264), which declares none: checked as a block's,
        // a failing one's details too, and the members around it kept.
        .name = "static assertions among members",
        .source = "struct S { int a; _Static_assert(1, \"\"); int b; };\n"
                  "struct T { _Static_assert(sizeof(int) == 4, \"x\"); int c; union { _Static_assert(1, \"y\"); int d; }; };\n"
                  "union U { _Static_assert(1, \"u\"); int a; float b; };\n"
                  "struct V { int a; _Static_assert(sizeof(int) == 3, \"fails\"); char b : 2; };\n"
                  "struct X { int a; _Static_assert(1, \"\"); };\n"
                  "int z = sizeof(struct S) + sizeof(struct T) + sizeof(union U) + sizeof(struct V) + sizeof(struct X);\n"
                  "void f(struct S *s) { s->b = s->a; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "C23 static assertions among members",
        .source = "struct S { int a; static_assert(1); int b; static_assert(sizeof(int) == 4, \"m\"); };\n"
                  "struct W { static_assert(1); };\n"
                  "int z = sizeof(struct S);\n",
        .c23 = true,
    },
};

// A parameter's typedef in a printed type (#1447 item 5, check_driver_placed_type): the declaration's own specifiers' and
// each nested parameter's typedef by place, with clang's a.k.a. of the type without them, for an object, a function, its
// address, an array's element, a typedef's use, a call's parameter, an initialized, assigned or passed target.
static const CheckDriverDefaultWarningFixture check_driver_parameter_typedef_fixtures[] =
{
    {
        .name = "parameters' typedefs",
        .source = "typedef int I;\n"
                  "typedef I J;\n"
                  "typedef void (*F)(I);\n"
                  "void f(I x);\n"
                  "void g(void (*cb)(I), J (*h)(I, J));\n"
                  "void use(void)\n"
                  "{\n"
                  "    int *p;\n"
                  "    void (*q)(I) = 0;\n"
                  "    F r = 0;\n"
                  "    p = q;\n"
                  "    p = r;\n"
                  "    p = f;\n"
                  "    p = &f;\n"
                  "    p = g;\n"
                  "    int (*s)(I*, J) = 0;\n"
                  "    p = s;\n"
                  "    I a[2];\n"
                  "    p = &a;\n"
                  "}\n",
        .errors = 7,
    },
    {
        .name = "parameters' typedefs by place",
        .source = "typedef int I;\n"
                  "typedef I J;\n"
                  "typedef const int CI;\n"
                  "typedef void (*F)(I);\n"
                  "typedef void G(I);\n"
                  "struct S { int x; };\n"
                  "typedef struct S T;\n"
                  "void take(void (*cb)(I));\n"
                  "void takef(F f);\n"
                  "void h(I a, ...);\n"
                  "void k(void);\n"
                  "int (*a)(I), (*b)(J *, T);\n"
                  "G *gp;\n"
                  "I (*ret)(I);\n"
                  "void (*arr[2])(I);\n"
                  "void (*pp)(CI *, I);\n"
                  "void (*kr)();\n"
                  "void use(int n, int *ip)\n"
                  "{\n"
                  "    void (*q)(I) = ip;\n"
                  "    int *p;\n"
                  "    p = h;\n"
                  "    p = a;\n"
                  "    p = b;\n"
                  "    p = gp;\n"
                  "    p = ret;\n"
                  "    p = arr;\n"
                  "    p = arr[0];\n"
                  "    p = *arr;\n"
                  "    p = pp;\n"
                  "    q = ip;\n"
                  "    take(ip);\n"
                  "    takef(ip);\n"
                  "    p = take;\n"
                  "    p = k;\n"
                  "    p = kr;\n"
                  "    void (*vla)(int (*)[2], I) = 0;\n"
                  "    p = vla;\n"
                  "    void (*na)(I, J (*)(T)) = 0;\n"
                  "    p = na;\n"
                  "    p = &na;\n"
                  "}\n",
        .errors = 19,
        .notes = 2,
    },
};

// A counted_by family member's type as clang's CountAttributedType prints it (#1842's 3c, check_driver_counted_type): each
// keyword's, a pointer's attribute before the placeholder and a flexible array's after it, through `&` and an array's
// decay, as a failed conversion's source or an assigned member target, with the a.k.a.; a sibling declarator without
// one keeps none.
static const CheckDriverDefaultWarningFixture check_driver_counted_sugar_fixtures[] =
{
    {
        .name = "counted_by family sugar",
        .source = "#define __counted_by(n) __attribute__((counted_by(n)))\n"
                  "struct B { int n; int *__counted_by(n) p; char *__attribute__((sized_by(n))) q; void *__attribute__((sized_by_or_null(n))) r; int *__attribute__((counted_by_or_null(n))) s; };\n"
                  "struct F { int n; int fam[] __counted_by(n); };\n"
                  "void f(struct B *b, struct F *g)\n"
                  "{\n"
                  "    long *l;\n"
                  "    l = b->p;\n"
                  "    l = b->q;\n"
                  "    l = b->r;\n"
                  "    l = b->s;\n"
                  "    l = g->fam;\n"
                  "    l = &b->p;\n"
                  "    double d = b->p;\n"
                  "    int *ok = b->p;\n"
                  "}\n",
        .errors = 6,
    },
    {
        .name = "counted_by sugar of targets and siblings",
        .source = "#define __counted_by(n) __attribute__((counted_by(n)))\n"
                  "struct B { int n; int *__counted_by(n) p; int *__counted_by(n) p2, *plain; };\n"
                  "struct F { int n; int fam[] __counted_by(n); };\n"
                  "void take(long *x);\n"
                  "void f(struct B *b, struct F *g, struct B v, long *l)\n"
                  "{\n"
                  "    b->p = l;\n"
                  "    v.p = l;\n"
                  "    take(b->p);\n"
                  "    take(g->fam);\n"
                  "    l = b->p2;\n"
                  "    l = b->plain;\n"
                  "    l = v.p;\n"
                  "    l = &g->fam;\n"
                  "    int *q = b->p;\n"
                  "}\n",
        .errors = 8,
        .notes = 2,
    },
    {
        // A member's specifiers' typedef past a GNU attribute list, whose arguments' names are no specifier (#2417: an
        // invalid counted_by's count printed as the member's typedef), and kept in the counted sugar (`'const T *
        // __counted_by(n)'`).
        .name = "counted_by sugar with typedefs",
        .source = "typedef int T;\n"
                  "struct B { int n; const int *__attribute__((counted_by(n + 1))) e; T __attribute__((aligned(8))) *t; __attribute__((aligned(8))) T *u; const T *__attribute__((counted_by(n))) v; };\n"
                  "void f(struct B *b, long *l)\n"
                  "{\n"
                  "    l = b->e;\n"
                  "    l = b->t;\n"
                  "    l = b->u;\n"
                  "    l = b->v;\n"
                  "}\n",
        .errors = 5,
    },
    {
        .name = "counted_by sugar of qualified typedefs",
        .source = "typedef const int CI;\n"
                  "typedef int *IP;\n"
                  "struct B { int n; CI *__attribute__((counted_by(n))) p; volatile CI *__attribute__((counted_by(n))) q; };\n"
                  "void f(struct B *b, long *l) { l = b->p; l = b->q; l = &b->p; }\n",
        .errors = 3,
    },
};

// A use of a counted_by or counted_by_or_null pointer member whose pointee type is incomplete there (#1842's 3d,
// check_graph_counted_uses): clang's "cannot use" at a read (a condition's, a `.`'s, a return's) and "cannot assign to" at
// an assignment, each with its two notes; none for `&`'s operand, a sized_by member, or a pointee completed before the use.
static const CheckDriverDefaultWarningFixture check_driver_counted_incomplete_fixtures[] =
{
    {
        .name = "counted_by pointee incomplete",
        .source = "#define __counted_by(n) __attribute__((counted_by(n)))\n"
                  "struct Inc;\n"
                  "struct B { int n; struct Inc *__counted_by(n) p; };\n"
                  "struct Inc *g(struct B *b, struct Inc *x)\n"
                  "{\n"
                  "    b->p = x;\n"
                  "    struct Inc *y = b->p;\n"
                  "    return b->p;\n"
                  "}\n"
                  "void h(struct B *b) { (void)b->p; void *v = b->p; struct Inc **pp = &b->p; }\n",
        .errors = 5,
        .notes = 10,
    },
    {
        .name = "counted_by pointee completed later",
        .source = "#define __counted_by(n) __attribute__((counted_by(n)))\n"
                  "struct Inc;\n"
                  "struct Late;\n"
                  "struct B { int n; struct Inc *__counted_by(n) p; struct Late *__counted_by(n) l; struct Inc *__attribute__((counted_by_or_null(n))) o; struct Inc *__attribute__((sized_by(n))) z; };\n"
                  "void early(struct B *b, struct B v)\n"
                  "{\n"
                  "    struct Late *a = b->l;\n"
                  "    struct Inc *c = v.p;\n"
                  "    struct Inc *d = b->o;\n"
                  "    struct Inc *e = b->z;\n"
                  "    if (b->p) { }\n"
                  "    b->o = 0;\n"
                  "    struct Inc **f = &v.p;\n"
                  "}\n"
                  "struct Late { int x; };\n"
                  "void late(struct B *b)\n"
                  "{\n"
                  "    struct Late *a = b->l;\n"
                  "    int k = b->l[0].x;\n"
                  "}\n",
        .errors = 5,
        .notes = 10,
    },
};

// Vector member failures retain the typed action's decision and specialize only its diagnostic (#2096).
static const CheckDriverDefaultWarningFixture check_driver_vector_member_fixtures[] =
{
    {
        .name = "vector member diagnostics through dot and arrow",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "typedef float g4 __attribute__((vector_size(16)));\n"
                  "float a(g4 *p) { return (*p).y; }\n"
                  "float b(v4 *p) { return (*p).q; }\n"
                  "void c(v4 *p) { (*p).xx = (*p).zw; }\n"
                  "float *d(v4 *p) { return &(*p).x; }\n"
                  "float e(g4 *p) { return p->y; }\n"
                  "float f(v4 *p) { return p->q; }\n"
                  "void g(v4 *p) { p->xx = p->zw; }\n"
                  "float *h(v4 *p) { return &p->x; }\n",
        .errors = 8,
    },
    {
        .name = "vector component characters and bounds",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "typedef float v2 __attribute__((ext_vector_type(2)));\n"
                  "typedef _Bool b4 __attribute__((ext_vector_type(4)));\n"
                  "void f(v4 v, v4 *p, v2 small, b4 bits)\n{\n"
                  "    (void)v.xq;\n    (void)v.xr;\n    (void)v.s0g;\n    (void)v.s;\n"
                  "    (void)v.s04;\n    (void)small.z;\n    (void)bits.x;\n    (void)bits.lo;\n"
                  "    (void)p->xq;\n    (void)p->s0g;\n    (void)p->s4;\n"
                  "    (void)p->  xq;\n    (void)v.  xq;\n"
                  "    (void)v.f;\n    (void)v.fq;\n    (void)v.ABC;\n}\n",
        .errors = 16,
    },
    {
        .name = "vector duplicate lvalues and element addresses",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "typedef float v2 __attribute__((ext_vector_type(2)));\n"
                  "void f(v4 v, const v4 c)\n{\n"
                  "    v.xx += v.zw;\n    c.xx = v.xy;\n    v.xx[0] = 1;\n"
                  "    (v + v).xx = v.xy;\n    (v + v).xx[0] = 1;\n"
                  "    (void)&v[1];\n    (void)&v.x;\n    (void)&v.xx;\n"
                  "    (void)&(v + v).x;\n    (void)&((v4){0}).x;\n"
                  "    v.xy = v.zw;\n    v.x += 1;\n}\n",
        .errors = 10,
    },
    {
        .name = "swizzle aliases exclude fields and typeof aliases",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "typedef v4 Alias;\n"
                  "struct S { __typeof__(((v4){0}).xy) wrong; Alias member; };\n"
                  "typedef __typeof__(((v4){0}).xy) T;\n"
                  "void f(v4 v, struct S s) { (void)&v.xx; s.member++; }\n"
                  "typedef float v2 __attribute__((ext_vector_type(2)));\n"
                  "void g(v4 v) { (void)&v.xx; }\n",
        .errors = 3,
    },
    {
        .name = "swizzle result names an aligned vector typedef",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "typedef float v2 __attribute__((ext_vector_type(2), aligned(32)));\n"
                  "void f(v4 v, const v4 c) { (void)&v.xx; (void)&c.xx; (void)v.xx.z; }\n",
        .errors = 3,
    },
    {
        .name = "valid vector members and ordinary records",
        .source = "typedef float v4 __attribute__((ext_vector_type(4)));\n"
                  "struct S { int q, xx; };\n"
                  "float f(v4 v, v4 *p, struct S *s)\n{\n"
                  "    v.xy = p->zw;\n    p->s01 += v.yx;\n    v.x++;\n"
                  "    s->xx = s->q;\n    int *q = &s->q;\n"
                  "    return v.x + p->y + v.lo[0] + v.hi[0] + v.even[0] + v.odd[0] + *q;\n}\n",
        // Swizzle lowering is an existing named refusal, independent of these semantic diagnostics.
        .syntax_only = true,
    },
};

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_type_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_const_assignment_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_const_assignment_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_convention_sugar_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_convention_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vla_bound_fixtures, BUSTER_ARRAY_LENGTH(check_driver_vla_bound_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_size_sugar_fixtures, BUSTER_ARRAY_LENGTH(check_driver_size_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_static_assert_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_static_assert_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_counted_incomplete_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_counted_incomplete_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_counted_sugar_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_counted_sugar_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_parameter_typedef_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_parameter_typedef_fixtures));
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vector_member_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_vector_member_fixtures));
}
