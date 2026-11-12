// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wunused-local-typedef sources (#2264, c_check_binding.c's check_input_unused_events). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_unused_local_typedef_fixtures[] =
{
    {
        // Unused block typedefs, a variable array's with a comma bound and a pointer to one among them, and one a
        // declaration's specifiers use.
        .name = "a block's unused typedefs, a variable array's among them",
        .source = "void f(int x, int n)\n"
                  "{\n"
                  "    typedef int T[(x, n)];\n"
                  "    typedef int U[n];\n"
                  "    typedef int V[4];\n"
                  "    typedef int (*W)[n];\n"
                  "    typedef int Y[n];\n"
                  "    Y y;\n"
                  "    (void)y;\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 5,
    },
    {
        // Given at the unit's end in source order, after the bodies' unused variables and runtime warnings: an inner
        // block's, a tag's typedef, shadowing ones; not an attribute unused one or a used one.
        .name = "unused typedefs at the unit's end",
        .source = "void f(int n)\n"
                  "{\n"
                  "    typedef int Z;\n"
                  "    int unused1;\n"
                  "    {\n"
                  "        typedef int Inner;\n"
                  "        int unused2;\n"
                  "    }\n"
                  "    typedef int Used;\n"
                  "    Used u = 0;\n"
                  "    (void)u;\n"
                  "    typedef struct S { int a; } SS;\n"
                  "}\n"
                  "int g(void) { int q; typedef long L; return 1 << 40; }\n"
                  "void h(void) { typedef int __attribute__((unused)) Q; typedef int R; R *p = 0; (void)p; typedef int G; { typedef int G; } }\n",
        .options = { "-Wall" },
        .warnings = 10,
    },
    {
        // After the tentative arrays' and the unused file-scope declarations' warnings, under their gate: none after
        // an error -Werror made before them.
        .name = "unused typedefs after the unused file-scope declarations",
        .source = "static void unused_fn(void) {}\n"
                  "int tent[];\n"
                  "void f(void) { typedef int Z; }\n"
                  "static int unused_var;\n"
                  "void g(void) { typedef int Y; int k; }\n",
        .options = { "-Wall" },
        .warnings = 6,
    },
    {
        // An unused function's error under -Werror comes after the gate, so the typedefs' still follow.
        .name = "unused typedefs after an unused function",
        .source = "void f(void) { typedef int Z; typedef int W; }\n"
                  "static void u(void) {}\n",
        .options = { "-Wall" },
        .warnings = 3,
    },
    {
        // Uses in another typedef, sizeof, a cast, an inner block, a prototype, a statement expression and a
        // declarator list; a macro's typedef comes last; a pragma ignores one.
        .name = "unused typedefs' uses",
        .source = "#define DECL(n) typedef int n\n"
                  "int f(int n)\n"
                  "{\n"
                  "    typedef int A; typedef A B;\n"
                  "    typedef long C; int s = sizeof(C);\n"
                  "    typedef short D; int t = (D)n;\n"
                  "    typedef char E; { E e = 0; (void)e; }\n"
                  "    typedef int F; void g(F);\n"
                  "    typedef int __attribute__((unused)) G;\n"
                  "    DECL(H);\n"
                  "    typedef struct { int x; } I; int z = ({ I i = { 1 }; i.x; });\n"
                  "    typedef int J[sizeof(int)];\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wunused-local-typedef\"\n"
                  "    typedef int K;\n"
                  "#pragma clang diagnostic pop\n"
                  "    typedef int L, M; L l = 0;\n"
                  "    return s + t + z + l;\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 4,
    },
};

static void check_test_driver_unused_local_typedef(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_local_typedef_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unused_local_typedef_fixtures));
}
