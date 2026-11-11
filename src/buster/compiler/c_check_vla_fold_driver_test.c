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

static const CheckDriverDefaultWarningFixture check_driver_vla_fold_fixtures[] =
{
    {
        .name = "misplaced variable-length arrays recover",
        .source = check_driver_vla_misplaced_source,
        .errors = 9,
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

static void check_test_driver_vla_fold(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vla_fold_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_vla_fold_fixtures));
}
