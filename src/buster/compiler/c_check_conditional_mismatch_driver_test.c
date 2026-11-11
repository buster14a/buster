// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a conditional's -Wpointer-type-mismatch and -Wconditional-type-mismatch (#2292,
// c_checker_driver.c's check_driver_conditional_mismatch_message). Clang supplies the external diagnostic oracle. Include
// after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels
// and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_conditional_mismatch_fixtures[] =
{
    {
        // At the `?`, naming both arms' types as written (a typedef's, an array's or a string's decayed pointer, function
        // pointers, records), a GNU `?:`'s left arm its condition; nested, parenthesized and through a macro.
        .name = "conditional pointer mismatches",
        .source = "int *p; long *q; int arr[3]; long larr[2];\n"
                  "void g(void) { void *r = 1 ? p : q; (void)r; }\n"
                  "typedef int I; I *pi; const char *cc; unsigned *u;\n"
                  "void h(int c) { (void)(c ? pi : q); (void)(c ? cc : u); (void)(c ? (p) : (q + 1)); }\n"
                  "void (*f1)(void); void (*f2)(int);\n"
                  "void k(int c) { (void)(c ? f1 : f2); (void)(c ?: q); }\n"
                  "struct S { int a; } *sp; union U { int b; } *up;\n"
                  "void m(int c) { (void)(c ? sp : up); (void)(c ? arr : larr); (void)(c ? \"x\" : q); (void)(c ? (c ? p : q) : q); }\n"
                  "#define SEL(c, a, b) ((c) ? (a) : (b))\n"
                  "void n(int c, int i) { (void)SEL(c, p, q); (void)(c ? i : p); (void)(c ? p : 1L); }\n",
        .warnings = 13,
        .notes = 1,
    },
    {
        // At file scope, before the initializer's own error.
        .name = "a file-scope conditional pointer mismatch",
        .source = "int *p; long *q;\n"
                  "void *r1 = 1 ? p : q;\n",
        .warnings = 1,
        .errors = 1,
    },
};

static void check_test_driver_conditional_mismatches(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_conditional_mismatch_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_conditional_mismatch_fixtures));
}
