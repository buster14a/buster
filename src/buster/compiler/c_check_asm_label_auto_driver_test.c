// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of clang's "ignored asm label 'X' on automatic variable" (#2370, c_check_asm_input.c's
// check_input_asm_labels). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_asm_label_auto_fixtures[] =
{
    {
        // An object of automatic storage, `auto` or none, an array, a VLA, a struct, each declarator of a list, in a nested
        // block, a label of several strings and escapes; not a register variable's, a static's, an extern's, a function's or
        // a typedef's, nor a file-scope object's.
        .name = "asm labels on automatic variables",
        .source = "int g asm(\"g1\");\n"
                  "void a(void) { auto int c asm(\"c1\"); int d asm(\"d1\") = 2, e asm(\"e1\"); (void)c; (void)d; (void)e; }\n"
                  "void b(void) { typedef int T asm(\"t1\"); int f(void) asm(\"ff\"); extern int x asm(\"xx\"); static int s asm(\"ss\"); "
                  "register int r asm(\"eax\"); (void)r; (void)s; }\n"
                  "void c(int k) { int vla[k] asm(\"v\\x41\"); (void)vla; { struct { int a; } st asm(\"st\" \"1\"); (void)st; } }\n"
                  "void d(void) { int q __asm__(\"q1\") __attribute__((unused)); int u asm(\"u1\"), w asm(\"w1\"); u = w = 1; }\n",
        .warnings = 8,
    },
    {
        // A for statement's declarations (#2419): each declarator's label, after a pointer's or a parenthesized declarator,
        // with attributes after it; a register variable's is no automatic one's.
        .name = "asm labels in for statements' declarations",
        .source = "void b(void) { for (int i asm(\"i1\") = 0; i < 1; i++) {} }\n"
                  "void c(void) { for (int i __asm__(\"i2\") __attribute__((unused)) = 0, j asm(\"j\") = 1; i < j; i++) {} }\n"
                  "void d(void) { for (register int r asm(\"ebx\") = 0; r < 1; r++) {} }\n"
                  "void e(void) { for (int *p asm(\"p1\") = 0, (*q) asm(\"q1\"), (k) asm(\"k1\"); ;) { (void)p; (void)q; (void)k; break; } }\n",
        .warnings = 6,
    },
    {
        // C23's `auto` of an inferred type and constexpr objects.
        .name = "asm labels on C23 automatic variables",
        .source = "void a(void) { auto x __asm__(\"x1\") = 1; (void)x; constexpr int y __asm__(\"y1\") = 2; (void)y; }\n",
        .warnings = 2,
        .c23 = true,
    },
};

static void check_test_driver_asm_label_auto(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_asm_label_auto_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_asm_label_auto_fixtures));
}
