// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wunused-but-set-variable sources (#2409, c_check_binding.c's check_input_discarded_set). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_unused_but_set_fixtures[] =
{
    {
        // Locals whose every use sets them, among the unused ones at each scope's end, in declaration order: plain,
        // compound and stepped writes, a static's, a struct's and a pointer's, a volatile's plain write; and those another
        // use reads (a member, an element, a read of itself, a tested write, a nested assignment, sizeof, typeof,
        // _Generic, a parenthesized target, a volatile's compound write, a statement expression's value).
        .name = "variables set but not used",
        .source = "struct P { int a; };\n"
                  "void f(int c) {\n"
                  "  int x; x = 1;\n"
                  "  int y = 2; y += 3;\n"
                  "  int z = 0; z++;\n"
                  "  int w = 0; w = w + 1;\n"
                  "  int v; int u;\n"
                  "  { int k; k = 2; }\n"
                  "  static int s; s = 4;\n"
                  "  struct P p; p.a = 1;\n"
                  "  struct P q; q = (struct P){0};\n"
                  "  int *ptr; ptr = 0;\n"
                  "  int arr[2]; arr[0] = 1;\n"
                  "  volatile int vol; vol = 1;\n"
                  "  volatile int v2 = 0; v2 += 1;\n"
                  "  int m; if ((m = c)) {}\n"
                  "  int n; n = c, n = 2;\n"
                  "  int t; t = 0, (void)0;\n"
                  "  int a; a = 1; (void)sizeof(a);\n"
                  "  int b; (b) = 1;\n"
                  "  int o; o = 1; __typeof__(o) o2 = 0; (void)o2;\n"
                  "  int r; r = 1; (void)_Generic(r, int: 1);\n"
                  "  int l, l2; l = l2 = 0;\n"
                  "  int g; g = 1; ({ g = 2; });\n"
                  "  int e __attribute__((unused)); e = 1;\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 12,
        .compare_warning_text = true,
    },
    {
        // A for header's clauses, an if's arms, a block's shadowing name, and a unit whose error drops its scope's.
        .name = "variables set but not used in control statements",
        .source = "void f(int c) { int d; for (d = 0; c; d++) {} int e; for (e = 0; e < c; e++) {} }\n"
                  "void g(int c) { int f2; if (c) f2 = 1; else f2 = 2; int g2; g2 = 1; { int g2; g2 = 2; } }\n"
                  "void h(int c) { for (int i = 0; c; i++) { int z; z = 2; } }\n",
        .options = { "-Wunused-but-set-variable" },
        .warnings = 6,
        .compare_warning_text = true,
    },
    {
        .name = "variables set but not used in a rejected unit",
        .source = "void f(int c) { int x; x = 1; c = undeclared; }\n"
                  "void h(void) { int a; a = 1; }\n",
        .options = { "-Wall" },
        .warnings = 1,
        .errors = 1,
        .syntax_only = true,
    },
    {
        .name = "variables set but not used by default",
        .source = "void f(void) { int x; x = 1; }\n",
        .compare_warning_text = true,
    },
};

static void check_test_driver_unused_but_set(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_but_set_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unused_but_set_fixtures));
}
