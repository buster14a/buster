// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original alignment builtin sources (#2221): __builtin_align_up's, __builtin_align_down's and __builtin_is_aligned's
// errors as clang words and places them (check_semantic_alignment), and their -Wtautological-compare of an alignment of 1
// (check_input_alignment_one). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_alignment_builtin_fixtures[] =
{
    {
        .name = "the alignment checks' errors, past 128 bits too, and an alignment of 1",
        .source = "int f(int x) { return __builtin_align_up(x, 0); }\n"
                  "int g(int x) { return __builtin_align_up(x, 1L << 40); }\n"
                  "int h(int x) { return __builtin_align_up(x, 3); }\n"
                  "_BitInt(200) k(_BitInt(200) x) { return __builtin_align_up(x, (unsigned _BitInt(300))1 << 200); }\n"
                  "int l(int x) { return __builtin_is_aligned(x, 1); }\n",
        .warnings = 1,
        .errors = 4,
        .notes = 0,
    },
    {
        .name = "the alignment checks' errors at clang's places",
        .source = "enum E { A, B };\n"
                  "int f1(int x) { return __builtin_align_up(x, (0)); }\n"
                  "int f2(int x) { return __builtin_align_down(x, -1); }\n"
                  "int f3(int x) { return __builtin_align_up(x, (long)6); }\n"
                  "char f4(char x) { return __builtin_align_up(x, 256); }\n"
                  "char *f5(char *x) { return __builtin_align_up(x, (__int128)1 << 64); }\n"
                  "enum E f6(enum E x) { return __builtin_align_up(x, 3); }\n"
                  "_Bool f7(unsigned short x) { return __builtin_is_aligned(x, 1 << 17); }\n"
                  "int f8(int x) { const int a = 0; return __builtin_align_up(x, a); }\n"
                  "static const int k = 5;\n"
                  "int f9(int x) { return __builtin_align_up(x, k); }\n"
                  "unsigned _BitInt(130) g(unsigned _BitInt(130) x) { return __builtin_align_up(x, (unsigned _BitInt(140))1 << 131); }\n"
                  "int f10(int x) { return __builtin_align_up(x, sizeof(int[3])); }\n",
        .warnings = 0,
        .errors = 11,
        .notes = 0,
    },
    {
        .name = "the alignment builtins' operand types",
        .source = "struct S { int a; };\n"
                  "double f1(double x) { return __builtin_align_up(x, 4); }\n"
                  "int f2(int x) { return __builtin_align_up(x, 4.0); }\n"
                  "int f3(struct S s) { return __builtin_is_aligned(s, 4); }\n"
                  "int f4(int x, int *p) { return __builtin_align_up(x, p); }\n"
                  "void f5(void (*fp)(void)) { (void)__builtin_align_up(fp, 4); }\n",
        .warnings = 0,
        .errors = 5,
        .notes = 2,
    },
    {
        .name = "an alignment of 1, unevaluated and under a pragma",
        .source = "int a(int x) { return __builtin_align_up(x, 1); }\n"
                  "int b(int x) { return __builtin_align_down(x, 1); }\n"
                  "int c(int x) { const int k = 1; return __builtin_is_aligned(x, k); }\n"
                  "int d(int x) { return __builtin_is_aligned(x, (char)1) + sizeof(__builtin_is_aligned(x, 1)); }\n"
                  "#pragma clang diagnostic ignored \"-Wtautological-compare\"\n"
                  "int e(int x) { return __builtin_is_aligned(x, 1); }\n",
        .warnings = 5,
        .errors = 0,
        .notes = 0,
    },
    {
        .name = "an alignment of 1 where the source's type is refused",
        .source = "struct S{int a;};enum E{A};\n"
                  "int a(double x) { return __builtin_is_aligned(x, 1); }\n"
                  "int b(int x) { return __builtin_is_aligned(x, 1); }\n"
                  "int c(struct S s) { return __builtin_is_aligned(s, 1); }\n"
                  "int d(enum E e) { return __builtin_is_aligned(e, 1); }\n"
                  "int e(void (*f)(void)) { return __builtin_is_aligned(f, 1); }\n"
                  "int g(int *p) { return __builtin_is_aligned(p, 1) + __builtin_is_aligned(p + 1, 1); }\n"
                  "int h(char a[4]) { char b[3]; return __builtin_is_aligned(b, 1) + __builtin_is_aligned(a, 1); }\n",
        .warnings = 5,
        .errors = 4,
        .notes = 2,
    },
};

static void check_test_driver_alignment_builtins(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_alignment_builtin_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_alignment_builtin_fixtures));
}
