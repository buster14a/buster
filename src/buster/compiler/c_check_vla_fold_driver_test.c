// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's folding of array bounds that are no integer constant expressions (#1447): a declarator's
// whose array can't be variable (at file scope, a member's, a block's static or extern object's) is folded with
// -Wgnu-folding-constant's "variable length array folded to constant array as an extension" at its name, once for the
// declarator; a bit-field's width, an enumerator's value, a case label, a designator and a parameter's bound fold
// silently, a block's automatic array stays variable, and an alignment or a type name's bound in a file-scope initializer
// is an error. A variable-length array or variably modified type where C allows none takes clang's error at its place
// (check_input_vla_place, check_input_vla_declaration): at file scope, a block's static or extern object, a member, and a
// sizeof's type name in a file-scope initializer. Each such source has one, as the reader stops at it where clang goes
// on. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and requested
// lanes.

static const char check_driver_vla_fold_contexts_source[] =
    "#define F (int)(1.0 + 2)\n"
    "int a1[F];\n"
    "int a2[2][F];\n"
    "int *a3[F];\n"
    "int (*a4)[F];\n"
    "typedef int T1[F];\n"
    "struct S { int m[F]; int bf : F; };\n"
    "enum E { X = F };\n"
    "_Static_assert(F, \"\");\n"
    "extern int a5[F];\n"
    "int a6[F], a7[F];\n"
    "void p(int x[F]);\n"
    "int f(int n)\n"
    "{\n"
    "    int b1[F];\n"
    "    static int b2[F];\n"
    "    extern int b3[F];\n"
    "    typedef int T2[F];\n"
    "    switch (n) { case F: return 1; }\n"
    "    int c = sizeof(int[F]);\n"
    "    _Alignas(F) int d;\n"
    "    return b1[0] + b2[0] + c + d;\n"
    "}\n"
    "int q[] = { [F] = 1 };\n"
    "_Alignas(F * 4) int e;\n";

static const char check_driver_vla_fold_declarators_source[] =
    "#define F (int)(1.0 + 2)\n"
    "int a[F][F];\n"
    "struct S { int x; int m[F][2]; int : F; };\n"
    "int z[F] = { 1, 2 };\n"
    "const int k = 2;\n"
    "int w[k + (int)(1.0 + 1)];\n"
    "void g(void) { static int q[F][F]; extern int r[F]; int ok[F]; (void)ok; (void)q; (void)r; }\n";

static const char check_driver_vla_fold_forms_source[] =
    "int a1[(int)2.5];\n"
    "int a2[(int)(1.0 + 2)];\n"
    "char a3[(unsigned)3.0f];\n"
    "int a4[(long)1.5 + 1];\n"
    "typedef int T1[(int)2.0];\n"
    "struct S { int m[(int)3.0]; int bf : (int)2.0; };\n"
    "int s1 = sizeof(int[(int)2.0]);\n"
    "enum E { X = (int)2.5 };\n"
    "_Static_assert((int)1.0, \"\");\n"
    "int f(int n)\n"
    "{\n"
    "    int b1[(int)2.5];\n"
    "    static int b2[(int)2.5];\n"
    "    int b3[n];\n"
    "    int b4[sizeof(b3) > 0 ? 1 : 2];\n"
    "    switch (n) { case (int)1.0: return 1; }\n"
    "    int c = sizeof(int[(int)2.0]);\n"
    "    return b1[0] + b2[0] + b4[0] + c;\n"
    "}\n"
    "int g1[__builtin_constant_p(1) ? 2 : 3];\n"
    "const int k = 3;\n"
    "int g2[k];\n"
    "int g3[(int)\"x\"[0] - 'x' + 1];\n";

static const char check_driver_vla_place_source_0[] =
    "#define F (int)(1.0 + 2)\n"
    "int s = 1 + sizeof(int[F]);\n";

static const char check_driver_vla_place_source_1[] =
    "#define F (int)(1.0 + 2)\n"
    "int g[sizeof(int[F])];\n";

static const char check_driver_vla_place_source_2[] =
    "#define F (int)(1.0 + 2)\n"
    "void f(void) { static int b[F]; extern int c[F]; static int d[sizeof(int[F])]; }\n";

static const char check_driver_vla_place_source_3[] =
    "#define F (int)(1.0 + 2)\n"
    "struct S { int m[sizeof(int[F])]; };\n";

static const char check_driver_vla_place_source_4[] =
    "int n;\n"
    "int g[2][n];\n";

static const char check_driver_vla_place_source_5[] =
    "int n;\n"
    "int (*p)[n];\n";

static const char check_driver_vla_place_source_6[] =
    "int n;\n"
    "typedef int T[n];\n";

static const char check_driver_vla_place_source_7[] =
    "int n;\n"
    "int s[2][2] = { { 1, 2 }, { [1] = 3 + (sizeof(int[n])) } };\n";

static const char check_driver_vla_place_source_8[] =
    "void f(int n) { extern int b[n]; }\n";

static const char check_driver_vla_place_source_9[] =
    "void f(int n) { extern int (*p)[n]; }\n";

static const char check_driver_vla_place_source_10[] =
    "void f(int n) { static _Thread_local int b[n]; }\n";

static const char check_driver_vla_place_source_11[] =
    "void f(int n) { struct S { int a; int (*p)[n]; }; }\n";

static const char check_driver_vla_place_source_12[] =
    "int n;\n"
    "union U { int m[2][n]; };\n";

static const char check_driver_vla_place_source_13[] =
    "int n;\n"
    "int (g)[n];\n";

static const char check_driver_vla_place_source_14[] =
    "int n;\n"
    "int ((g))[2][n];\n";

static const char check_driver_vla_place_source_15[] =
    "int n;\n"
    "int *(p)[n];\n";

static const char check_driver_vla_place_source_16[] =
    "int n;\n"
    "int (*(p))[n];\n";

// Misplaced variable-length arrays and variably modified types (#1447): each error recovers, the declaration invalid and
// its uses quiet, a file-scope one's group going on.
static const char check_driver_vla_misplaced_source[] =
    "int n;\n"
    "int a[n];\n"
    "int (*vp)[n];\n"
    "int b[n], c;\n"
    "int use = a[0] + c + *vp[0];\n"
    "void f(int k) { static int s[k]; extern int e[k]; extern int (*ep)[k]; int u = q + s[0] + e[0]; (void)u; }\n"
    "typedef int T[n];\n"
    "int z = w;\n";

// Scalar compound literals in bounds (#1447): folded, with clang's warning where the array can't be variable, an
// enumerator's quietly; an `&` keeps the literal's object.
static const char check_driver_vla_compound_source[] =
    "int a[(int){3}];\n"
    "int b[(long){2 * 4} + 1];\n"
    "struct S { int m[(int){2}]; };\n"
    "void f(void) { static int s[(int){4}]; int t[(int){5}]; int *p = &(int){1}; (void)s; (void)t; (void)p; }\n"
    "int c[(const int){2}];\n"
    "enum { E = (int){1} };\n"
    "int e = (int){3} + E;\n"
    "int sz = sizeof((int){1});\n"
    "_Static_assert(sizeof(a) == 12 && sizeof(b) == 36, \"\");\n";

// A block's automatic variable-length array with an initializer (#1447): clang's "variable-sized object may not be
// initialized", at the outermost bound for a list and at the name otherwise, recovered; the declaration stays valid and its
// initializer unchecked, and `= {}` is taken.
static const char check_driver_vla_initializer_source[] =
    "void f(int n) {\n"
    "  int a[n] = {1};\n"
    "  int b[n + 1][2] = {{1}};\n"
    "  char s[n] = \"ab\";\n"
    "  int c[(n)] = {1, 2}, d = q1;\n"
    "  int e[n] = 5;\n"
    "  int use = a[0] + b[0][0] + s[0] + c[0] + e[0] + d;\n"
    "  int (*p)[n] = 0;\n"
    "  char *bad = use;\n"
    "  int g[n] = {1, \"x\"}; char *t3 = g;\n"
    "  int h[n] = {}; char *t4 = h;\n"
    "  (void)p; (void)bad; (void)t3; (void)t4;\n"
    "}\n"
    "int x = y;\n";

// A file-scope cast's or _Alignof's variable type name (#1852), which clang takes and never evaluates.
static const char check_driver_vla_unevaluated_source[] =
    "int n; void *p = (int(*)[n])0;\n"
    "int n2; int s = (int)(long)(int(*)[n2])0;\n"
    "int n3; int s3 = _Alignof(int[n3]);\n"
    "_Static_assert(_Alignof(long[n3]) == 8, \"\");\n"
    "int s4 = _Alignof(int[n3][4]) + sizeof(int);\n"
    "char *bad = (int(*)[n])0;\n"
    "int s5 = sizeof(int[n]);\n";

// A variably modified type can still have a constant layout (#2528). The pointer ends the runtime-size walk; a fixed
// array of such pointers stays constant too. The comma bound is still checked, so its ordinary warning is retained.
static const char check_driver_vla_pointer_size_source[] =
    "int n;\n"
    "unsigned long c = sizeof(int(*)[n]);\n"
    "unsigned long a = sizeof(int(*)[(3, 2)]);\n"
    "unsigned long p = sizeof(int(*[3])[n]);\n"
    "unsigned long m = sizeof(int(*)[n][n + 1]);\n"
    "enum { P = sizeof(int(*)[++n]), A = sizeof(int(*[3])[n]) };\n"
    "_Static_assert(P == 8 && A == 24, \"fixed pointer size\");\n"
    "_Static_assert(_Alignof(int[n]) == 4 && _Alignof(int(*)[n]) == 8, \"fixed alignment\");\n"
    "unsigned long nested_p = sizeof(int(*)[sizeof(int[n])]);\n"
    "unsigned long nested_a = _Alignof(int[sizeof(int[n])]);\n"
    "unsigned long nested_s = sizeof(sizeof(int[n]));\n"
    "unsigned long nested_side = sizeof(sizeof(int[++n]));\n";

static const char check_driver_vla_pointer_size_error_source[] =
    "int n;\n"
    "unsigned long c = sizeof(int(*)[n]);\n"
    "unsigned long a = sizeof(int(*)[(3, 2)]);\n"
    "unsigned long d = sizeof(int[n]);\n";

// A cast to a variably modified type in a body (#2321, #1852): its type name's bounds are evaluated before its operand,
// as clang's EmitVariablyModifiedType does, a sizeof's in an unevaluated operand not; no comma of its own warns, and
// -Wunsequenced, whose visit reaches no type, doesn't read them.
static const char check_driver_vla_cast_source[] =
    "int f(int n, int *v) { return (int)sizeof(*(int (*)[n])v); }\n"
    "int g(int n, int *v) { int (*p)[n] = (int (*)[n])v; return (int)sizeof *p; }\n"
    "void h(int n) { (void)((int (*)[n])0); }\n"
    "void k(int n) { (void)sizeof((int (*)[n])0); }\n"
    "void a(int n, int *v) { (int (*)[n])v; }\n"
    "int b(int n, int x) { return (int)sizeof(x++, (int(*)[n])0) + x; }\n"
    "void d(int i) { (void)sizeof((int (*)[i++])0); }\n"
    "void e(int n, int *v) { if ((int (*)[n])v) {} }\n"
    "void q(int n, int *v) { (void)(typeof(int[n]) *)v; }\n"
    "int r(int n, int *v) { return ((int (*)[n])v == (int (*)[n + 1])v); }\n"
    "int m(int n, int *v) { return _Alignof(int[n]) + (int)_Generic((int (*)[n])v, int (*)[]: 1, default: 2); }\n"
    "int u1(int n, int *v) { return ((int (*)[n++])v)[0][0] + n; }\n"
    "int u2(int n) { return (int)sizeof(int[n++]) + n; }\n"
    "int u3(int n, int *v) { return (int)sizeof(*(int (*)[n++])v) + n; }\n";

// The comma operators of constant bounds, bit-field widths and enumerators, which no graph walk reads (#2202): each
// discarded left operand's -Wunused-value "left operand of comma operator has no effect" as clang's isUnusedResultAWarning
// decides and places it (a binary or unary operator's at it, a cast's at its `(`, sizeof's at its keyword, a comma's at its
// right operand's but a literal 0's), -Wunused-comparison's for a comparison, none for a cast to void or in sizeof's
// operand, and in a dead conditional arm too; before the declarator's fold warning at file scope, and in a function body
// with its queued diagnostics at its end.
static const char check_driver_vla_fold_commas_source[] =
    "int a1[(1, 2)];\n"
    "int a2[1 ? 2 : (3, 4)];\n"
    "int a3[(1, 2, 3)];\n"
    "int a4[((void)0, 2)];\n"
    "int a5[(1 + 1, 2)];\n"
    "int a6[(sizeof(int), 2)];\n"
    "int a7[sizeof((1, 2))];\n"
    "int a8[((1, 0), 2)];\n"
    "int a9[(1 == 1, 2)];\n"
    "int a10[((char)1, 2)];\n"
    "int a11[(-1, 2) + (3, 4)];\n"
    "typedef int T[(5, 6)];\n"
    "struct S { int m[(7, 8)]; int n : (1, 3); };\n"
    "enum { E1 = (1, 2), E2 = ((void)0, 3), E3 = (E1, 4) };\n"
    "int f(void)\n"
    "{\n"
    "    static int s[(1, 2)];\n"
    "    enum { L = (3, 4) };\n"
    "    int u = 1 / 0;\n"
    "    return s[0] + L + u;\n"
    "}\n";

// The comma operators of variable bounds only the constant reader reads (#2252): a file-scope cast's and _Alignof's type
// names, a file-scope array's and member's folded bounds and an enumerator's beside them, and a body's cast's and
// sizeof's variable type names.
static const char check_driver_vla_variable_commas_source[] =
    "void *g = (int(*)[(5, 2)])0;\n"
    "unsigned long al = _Alignof(int[(4, 2)]);\n"
    "int a[(1, 2)];\n"
    "enum { A = (3, 4) };\n"
    "struct S { int m[(6, 3)]; };\n"
    "void f(void) { void *p = (int(*)[(7, 2)])0; (void)p; }\n"
    "void k(int n) { unsigned long s = sizeof(int[(8, n)]); (void)s; }\n";

// A function body's constants' commas among its queued diagnostics, in source order as clang queues them (#2252),
// though the reader finds them first: enumerators' values, a member's bound and width, a static array's bound, in nested
// blocks, in an if's, a do's and a for's bodies, beside the graph's variable typedef bound, array bounds and a division
// by zero, in two functions.
static const char check_driver_vla_body_commas_source[] =
    "int f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[5] = 0;\n"
    "    enum { B = (9, 1), C = ((void)0, 2), D = (B, 3) };\n"
    "    typedef int T[(7, 2)];\n"
    "    a[6] = 1;\n"
    "    enum { E = (4, 5) };\n"
    "    struct S { int m[(1, 2)]; int w : (3, 4); };\n"
    "    int u = 1 / 0;\n"
    "    {\n"
    "        static int s[(5, 2)];\n"
    "        a[7] = s[0];\n"
    "        {\n"
    "            enum { G = (6, 7) };\n"
    "            a[8] = G;\n"
    "        }\n"
    "    }\n"
    "    a[9] = (int)sizeof(T) + E;\n"
    "    return u;\n"
    "}\n"
    "int g(int n)\n"
    "{\n"
    "    int b[2];\n"
    "    b[2] = n;\n"
    "    enum { H = (8, 9) };\n"
    "    if (n) { enum { I = (1, 2) }; b[3] = I; }\n"
    "    do { enum { J = (3, 4) }; } while (b[4]);\n"
    "    for (int i = b[5]; i < 1; i++) { enum { K = (5, 6) }; }\n"
    "    return H;\n"
    "}\n";

// A body's constants' comparisons and `void *` cast as a comma's left operand, which clang gives at once rather than
// queued (#2252): before the body's queued diagnostics, a plain comma's among them.
static const char check_driver_vla_body_comparisons_source[] =
    "void f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[5] = 0;\n"
    "    enum { B = (1 == 1, 2), C = ((void *)0, 3), D = (4, 5), E = (1 < 2, 6) };\n"
    "    a[6] = 1;\n"
    "    typedef int T[(2 != 3, 2)];\n"
    "    a[7] = 1;\n"
    "    (void)sizeof(T);\n"
    "}\n";

// A block's static object's sizeof of a variable bound: its initializer's error at once, its comma with the body's queued
// diagnostics, among them in source order (#2252).
static const char check_driver_vla_static_commas_source[] =
    "void f(void)\n"
    "{\n"
    "    int a[2];\n"
    "    a[3] = 0;\n"
    "    static int b = sizeof(int[(6, 2)]);\n"
    "    enum { E = (1, 2) };\n"
    "    a[4] = 0;\n"
    "}\n";

// A constant's comma inside a body's full expression, which clang queues as it reads it, before what the full expression
// queues as it completes (#2252, pp_diagnostic_queued): array indexes before and after it, an unused result, unsequenced
// modifications, a constant conversion and a call's argument, each after a queued array index of an earlier statement.
static const char check_driver_vla_body_completions_source[] =
    "void g(int);\n"
    "int f(int i)\n"
    "{\n"
    "    int a[3];\n"
    "    char c;\n"
    "    a[3] = 0;\n"
    "    a[4] = (int)sizeof(struct { int m[(1, 2)]; });\n"
    "    int x = a[5] + (int)sizeof(enum { E = (3, 4) }) + a[6];\n"
    "    1 + (int)sizeof(struct { int m[(5, 6)]; });\n"
    "    i++ + i++ + (int)sizeof(struct { int m[(7, 8)]; });\n"
    "    c = 300 + (int)sizeof(struct { int m[(9, 1)]; });\n"
    "    g(a[7] + (int)sizeof(struct { int m[(2, 3)]; }));\n"
    "    return x + c;\n"
    "}\n";

// The same in statement expressions (a call's argument's, one whose own statement's warnings come first), before a null
// dereference's reader, in a division's and a shift's right operands, and in a for statement's clauses, whose third
// completes before its body (#2252).
static const char check_driver_vla_body_nested_source[] =
    "void g(int, int);\n"
    "void f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[3] = 0;\n"
    "    a[4] = ({ enum { E = (1, 2) }; 0; });\n"
    "    g(a[5], ({ static int s[(3, 4)]; s[0]; }));\n"
    "    a[6] = ({ a[7]; enum { F = (5, 6) }; 0; });\n"
    "    int x = *(int *)0 + (int)sizeof(struct { int m[(7, 8)]; });\n"
    "    x = x / (sizeof(struct { int m[(9, 2)]; }) - 8);\n"
    "    x = 1 << (sizeof(struct { int m[(1, 2)]; }) * 8);\n"
    "    for (a[8] = 0; (int)sizeof(struct { int m[(3, 1)]; }) > a[9]; a[10]++)\n"
    "    {\n"
    "        enum { G = (4, 5) };\n"
    "    }\n"
    "    (void)x;\n"
    "}\n";

// What building an operator queues comes before a constant's comma later in its full expression (#2252): a division's, a
// shift's, an atomic member's and a comma's left operand's.
static const char check_driver_vla_body_operators_source[] =
    "_Atomic struct A { int m; } s;\n"
    "int y;\n"
    "int f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[3] = 0;\n"
    "    int x = 1 / 0 + (int)sizeof(struct { int m[(1, 2)]; });\n"
    "    x += (1 << 40) + (int)sizeof(struct { int m[(3, 4)]; });\n"
    "    x += s.m + (int)sizeof(struct { int m[(5, 6)]; });\n"
    "    x += (y, 1) + (int)sizeof(struct { int m[(7, 8)]; });\n"
    "    return x;\n"
    "}\n";

// A comparison's warnings, which clang analyzes as its full expression completes, after a constant's comma it holds
// (#2252): a constant out of the operand's range, and a sign mismatch under -Wsign-compare.
static const char check_driver_vla_body_comparison_queue_source[] =
    "int i;\n"
    "unsigned u;\n"
    "unsigned char c;\n"
    "int f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[3] = 0;\n"
    "    int x = (i < u) + (int)sizeof(struct { int m[(1, 2)]; });\n"
    "    x += (c < 300) + (int)sizeof(struct { int m[(3, 4)]; });\n"
    "    return x;\n"
    "}\n";

// An x86 builtin's argument's range error, which clang queues as it builds the call, after a constant's comma in the
// argument (#2252).
static const char check_driver_vla_body_builtin_source[] =
    "typedef int v4si __attribute__((vector_size(16)));\n"
    "v4si f(v4si x)\n"
    "{\n"
    "    x = __builtin_ia32_pshufd(x, 300);\n"
    "    x = __builtin_ia32_pshufd(x, (int)sizeof(struct { int m[(1, 2)]; }) + 300);\n"
    "    return x;\n"
    "}\n";

// A body's member bounds' and bit-fields' widths' comparisons as commas' left operands, at once, before each declarator's
// fold warning and among the body's queued diagnostics (#2252).
static const char check_driver_vla_body_member_comparisons_source[] =
    "void f(void)\n"
    "{\n"
    "    int a[3];\n"
    "    a[5] = 0;\n"
    "    struct S { int m[(1 < 2, 2)]; int w : (3 != 3, 4); };\n"
    "    a[6] = 1;\n"
    "    union U { char n[(5 == 5, 1)]; int v : (6 >= 7, 2); };\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_vla_fold_fixtures[] =
{
    {
        .name = "a body's casts to variably modified types",
        .source = check_driver_vla_cast_source,
        .warnings = 2,
    },
    {
        .name = "misplaced variable-length arrays recover",
        .source = check_driver_vla_misplaced_source,
        .errors = 9,
    },
    {
        .name = "scalar compound literals in bounds",
        .source = check_driver_vla_compound_source,
        .warnings = 5,
    },
    {
        .name = "a file-scope cast's and _Alignof's variable type names",
        .source = check_driver_vla_unevaluated_source,
        .errors = 2,
    },
    {
        .name = "a file-scope sizeof of pointers to variable arrays stays constant",
        .source = check_driver_vla_pointer_size_source,
        .warnings = 1,
    },
    {
        .name = "a file-scope sizeof refuses only the runtime-sized array",
        .source = check_driver_vla_pointer_size_error_source,
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a file-scope evaluated nested variable array size remains nonconstant",
        .source = "int n; unsigned long x=sizeof(int[sizeof(int[n])]);\n",
        .errors = 1,
    },
    {
        .name = "a file-scope unevaluated nested size still checks its array bound type",
        .source = "unsigned long x=sizeof(sizeof(int[1.5]));\n",
        .errors = 1,
    },
    {
        .name = "a block's variable-length array with an initializer",
        .source = check_driver_vla_initializer_source,
        .errors = 11,
    },
    {
        .name = "variable bounds' comma operators the constant reader alone reads",
        .source = check_driver_vla_variable_commas_source,
        .warnings = 9,
    },
    {
        .name = "a body's constants' commas among its queued diagnostics in source order",
        .source = check_driver_vla_body_commas_source,
        .warnings = 24,
        .notes = 9,
    },
    {
        .name = "a body's constants' comparisons as commas' left operands at once",
        .source = check_driver_vla_body_comparisons_source,
        .warnings = 8,
        .notes = 3,
    },
    // A file-scope sizeof's variable bound's commas, which clang gives as it reads them, before its initializer's "not a
    // compile-time constant" once the initializer is read (#2252): alone, beside a folded bound in a braced list, with an
    // _Alignof's in a member's, and after a designator.
    {
        .name = "a file-scope sizeof's variable bound's comma before its initializer's error",
        .source = "int x;\nint b = sizeof(int[(6, 2)]);\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a braced file-scope sizeof's two variable bounds' commas before its error",
        .source = "int a[(1, 2)] = { sizeof(int[(6, 2)][(8, 3)]) };\n",
        .warnings = 4,
        .errors = 1,
    },
    {
        .name = "a member's _Alignof's and sizeof's variable bounds' commas before its error",
        .source = "struct S { int m; } s = { _Alignof(int[(4, 2)]) + sizeof(int[(5, 2)]) };\n",
        .warnings = 2,
        .errors = 1,
    },
    {
        .name = "a designated file-scope sizeof's variable bound's comma before its error",
        .source = "int b[] = { 1, [2] = sizeof(int[(6, 2)]) };\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a block's static sizeof's error before its body's queued commas",
        .source = check_driver_vla_static_commas_source,
        .warnings = 4,
        .errors = 1,
        .notes = 2,
    },
    // A file-scope sizeof's comma in a later declarator's initializer, or before a later declarator, whose initializer's
    // error is at its own `,` or `;` (#2252, check_driver_initializer_last).
    {
        .name = "a second declarator's sizeof's variable bound's comma before its error",
        .source = "int b = 1, c = sizeof(int[(6, 2)]);\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a middle declarator's sizeof's variable bound's comma before its error",
        .source = "int b = 1, c = sizeof(int[(6, 2)]), d = 2;\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a first declarator's sizeof's variable bound's comma before its error",
        .source = "int b = sizeof(int[(6, 2)]), c = 1;\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a body's constants' commas before what their full expressions queue as they complete",
        .source = check_driver_vla_body_completions_source,
        .warnings = 20,
        .notes = 5,
    },
    {
        .name = "a body's constants' commas in statement expressions, operators' operands and a for statement's clauses",
        .source = check_driver_vla_body_nested_source,
        .warnings = 25,
        .notes = 9,
    },
    {
        .name = "what building a body's operators queues before a later constant's comma",
        .source = check_driver_vla_body_operators_source,
        .warnings = 12,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a body's comparisons' constant warnings after a constant's comma they hold",
        .source = check_driver_vla_body_comparison_queue_source,
        .warnings = 6,
        .notes = 1,
    },
    {
        .name = "a body's comparisons' sign warnings after a constant's comma they hold",
        .source = check_driver_vla_body_comparison_queue_source,
        .options = { "-Wsign-compare" },
        .warnings = 7,
        .notes = 1,
    },
    {
        .name = "an x86 builtin's range error after a constant's comma in its argument",
        .source = check_driver_vla_body_builtin_source,
        .warnings = 2,
        .errors = 2,
    },
    {
        .name = "a body's members' and bit-fields' comparisons as commas' left operands at once",
        .source = check_driver_vla_body_member_comparisons_source,
        .warnings = 8,
        .notes = 2,
    },
    {
        .name = "constant bounds' comma operators' discarded left operands",
        .source = check_driver_vla_fold_commas_source,
        .warnings = 31,
    },
    {
        .name = "constant bounds' comma operators under -Wno-unused-value",
        .source = check_driver_vla_fold_commas_source,
        .options = { "-Wno-unused-value" },
        .warnings = 13,
    },
    {
        .name = "constant bounds' comma operators under -Wno-unused-value -Wunused-comparison",
        .source = check_driver_vla_fold_commas_source,
        .options = { "-Wno-unused-value", "-Wunused-comparison" },
        .warnings = 14,
    },
    {
        .name = "folded bounds in each context, by default",
        .source = check_driver_vla_fold_contexts_source,
        .warnings = 11,
        .errors = 2,
        .notes = 2,
    },
    {
        .name = "folded bounds in each context under -w",
        .source = check_driver_vla_fold_contexts_source,
        .options = { "-w" },
        .errors = 2,
        .notes = 2,
    },
    {
        .name = "folded bounds in each context under -Werror=gnu-folding-constant",
        .source = check_driver_vla_fold_contexts_source,
        .options = { "-Werror=gnu-folding-constant" },
        .errors = 18,
        .notes = 7,
    },
    {
        .name = "a declarator's folded bounds warn once, at its name",
        .source = check_driver_vla_fold_declarators_source,
        .warnings = 6,
    },
    {
        .name = "folds of casts, GNU object reads and string subscripts",
        .source = check_driver_vla_fold_forms_source,
        .warnings = 3,
    },
    {
        .name = "folded bounds under -Wno-gnu-folding-constant",
        .source = check_driver_vla_fold_forms_source,
        .options = { "-Wno-gnu-folding-constant" },
    },
    {
        .name = "a type name's folded-looking bound in a file-scope initializer stays variable",
        .source = check_driver_vla_place_source_0,
        .errors = 1,
    },
    {
        .name = "a file-scope bound that holds a variable type name",
        .source = check_driver_vla_place_source_1,
        .errors = 1,
    },
    {
        .name = "a block's static array whose bound holds a variable type name",
        .source = check_driver_vla_place_source_2,
        .warnings = 2,
        .errors = 1,
    },
    {
        .name = "a member whose bound holds a variable type name",
        .source = check_driver_vla_place_source_3,
        .errors = 1,
    },
    {
        .name = "a file-scope variable-length array",
        .source = check_driver_vla_place_source_4,
        .errors = 1,
    },
    {
        .name = "a file-scope variably modified pointer",
        .source = check_driver_vla_place_source_5,
        .errors = 1,
    },
    {
        .name = "a file-scope variable-length typedef",
        .source = check_driver_vla_place_source_6,
        .errors = 1,
    },
    {
        .name = "a list item's variable type name in a file-scope initializer",
        .source = check_driver_vla_place_source_7,
        .errors = 1,
    },
    {
        .name = "a block's extern variable-length array",
        .source = check_driver_vla_place_source_8,
        .errors = 1,
    },
    {
        .name = "a block's extern variably modified pointer",
        .source = check_driver_vla_place_source_9,
        .errors = 1,
    },
    {
        .name = "a block's static thread-local variable-length array",
        .source = check_driver_vla_place_source_10,
        .errors = 1,
    },
    {
        .name = "a block's record's variably modified member",
        .source = check_driver_vla_place_source_11,
        .errors = 1,
    },
    {
        .name = "a file-scope record's variable-length member",
        .source = check_driver_vla_place_source_12,
        .errors = 1,
    },
    {
        .name = "a parenthesized file-scope variable-length array",
        .source = check_driver_vla_place_source_13,
        .errors = 1,
    },
    {
        .name = "a nested parenthesized file-scope variable-length array",
        .source = check_driver_vla_place_source_14,
        .errors = 1,
    },
    {
        .name = "a parenthesized array of pointers at file scope, a variable-length array",
        .source = check_driver_vla_place_source_15,
        .errors = 1,
    },
    {
        .name = "a parenthesized pointer to a variable-length array at file scope, a variably modified type",
        .source = check_driver_vla_place_source_16,
        .errors = 1,
    },
};

// A body's casts to variably modified types run as clang's build's do (#2321): each bound once, before the operand, a
// sizeof's of the cast's value read from it, nested and comma bounds too; by both paths, at Tier 0 and Tier 1.
static const char check_driver_vla_cast_program[] =
    "int printf(const char*, ...);\n"
    "static int calls;\n"
    "static int k(int v) { calls = calls * 10 + v; return v; }\n"
    "int f(int n, int *v){ return (int)sizeof(*(int (*)[n])v); }\n"
    "int g(int n, int *v){ int (*p)[n] = (int (*)[n])v; return (int)sizeof *p + p[1][0]; }\n"
    "int h(int n, int *v) { return ((int (*)[k(n)])(v + k(1)))[1][1]; }\n"
    "int s(int i) { (void)sizeof((int (*)[i++])0); return i; }\n"
    "int c2(int n, int *v) { return (int)sizeof(*(int (*)[(k(2), n)])v); }\n"
    "long d(int n, int m, int *v) { return (long)sizeof(*(int (*)[n][m])v) + ((int (*)[n][m])v)[0][1][1]; }\n"
    "int e(int n, void *v) { return (*(int (*)[n])v)[2] + (int)sizeof(*(char(*)[n + 1])(int (*)[n])v); }\n"
    "int t(int n, int *v) { return ((int (*)[k(n)][k(2)])v)[1][1][0] + calls; }\n"
    "int main(void) {\n"
    "    int a[24] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };\n"
    "    printf(\"%d %d\\n\", f(3, a), g(3, a));\n"
    "    calls = 0; printf(\"%d %d\\n\", h(4, a), calls);\n"
    "    printf(\"%d\\n\", s(5));\n"
    "    calls = 0; printf(\"%d %d\\n\", c2(5, a), calls);\n"
    "    printf(\"%ld %d\\n\", d(2, 3, a), e(4, a));\n"
    "    calls = 0; printf(\"%d\\n\", t(3, a));\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_vla_cast_runs(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const ways[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    char source[512];
    char outputs[5][512];
    snprintf(source, sizeof(source), "%s/vla-casts.c", directory);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        snprintf(outputs[k], sizeof(outputs[k]), "%s/vla-casts-%u", directory, k);
    }
    const bool written = parse_test_write(source, check_driver_vla_cast_program, strlen(check_driver_vla_cast_program));
    char* output = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-w", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &output) : 1;
    free(output);
    output = NULL;
    static char expected[4096];
    const bool referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, referenced && strlen(expected) != 0, "variably modified casts: clang's build runs");
    for (uint32_t mode = 0; referenced && mode < 4; mode += 1)
    {
        char* arguments[16] = { (char*)executable };
        uint32_t count = 1;
        if ((mode & 1) != 0)
        {
            arguments[count++] = "--scalar";
        }
        if (mode >= 2)
        {
            arguments[count++] = "--tier1";
            arguments[count++] = "--verify";
        }
        arguments[count++] = "-o";
        arguments[count++] = outputs[mode];
        arguments[count++] = source;
        arguments[count] = NULL;
        const int status = check_test_driver_spawn(arguments, &output);
        static char captured[4096];
        const bool ran = status == 0 && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "variably modified casts evaluate their bounds once, before their operands, as clang's build does (%s)", ways[mode]);
        check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
        if (status != 0 && output != NULL)
        {
            fprintf(stderr, "%s", output);
        }
        else if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "variably modified casts (%s):\n%s\nclang's build:\n%s\n", ways[mode], captured, expected);
        }
        free(output);
        output = NULL;
        unlink(outputs[mode]);
    }
    unlink(outputs[4]);
    unlink(source);
}

static void check_test_driver_vla_fold(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vla_fold_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_vla_fold_fixtures));
    check_test_driver_vla_cast_runs(test, clang, executable, directory);
}
