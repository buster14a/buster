// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's folding of array bounds that are no integer constant expressions (#1447): a declarator's
// whose array can't be variable (at file scope, a member's, a block's static or extern object's) is folded with
// -Wgnu-folding-constant's "variable length array folded to constant array as an extension" at its name, once for the
// declarator; a bit-field's width, an enumerator's value, a case label, a designator and a parameter's bound fold
// silently, a block's automatic array stays variable, and an alignment or a type name's bound in a file-scope initializer
// is an error. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and
// requested lanes.

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

static const CheckDriverDefaultWarningFixture check_driver_vla_fold_fixtures[] =
{
    {
        .name = "folded bounds in each context, by default",
        .source = check_driver_vla_fold_contexts_source,
        .warnings = WC1,
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
        .errors = EC1,
        .notes = 2,
    },
    {
        .name = "a declarator's folded bounds warn once, at its name",
        .source = check_driver_vla_fold_declarators_source,
        .warnings = WC2,
    },
    {
        .name = "folds of casts, GNU object reads and string subscripts",
        .source = check_driver_vla_fold_forms_source,
        .warnings = WC3,
    },
    {
        .name = "folded bounds under -Wno-gnu-folding-constant",
        .source = check_driver_vla_fold_forms_source,
        .options = { "-Wno-gnu-folding-constant" },
    },
};

static void check_test_driver_vla_fold(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_vla_fold_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_vla_fold_fixtures));
}
