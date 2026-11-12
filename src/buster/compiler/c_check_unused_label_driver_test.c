// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wunused-label sources (#2264, c_check_control_input.c's check_input_unused_labels). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_unused_label_fixtures[] =
{
    {
        // A label no goto or address names, a statement expression's too, and a local label's at its `__label__` list;
        // one named by a goto, an address or an inner list's own label isn't; unused and maybe_unused mark one used.
        .name = "unused labels",
        .source = "void f(int x) { l: x++; }\n"
                  "void g(int x) { a: b: x++; goto b; }\n"
                  "void h(void) { __label__ m; m: ; }\n"
                  "int k(int x) { void *p = &&q; q: return x + (p != 0); }\n"
                  "void n(int x) { __attribute__((unused)) u: x++; v: __attribute__((unused)); [[maybe_unused]] w: ; }\n"
                  "void o(int x) { if (x) { z: ; } }\n"
                  "#define L(n) n: ;\n"
                  "void q(void) { L(w) }\n"
                  "int r(int x) { return ({ y: x; }); }\n"
                  "void s(void) { l: ; { __label__ l; l: ; goto l; } }\n"
                  "void t(int x) { switch (x) { case 1: l: ; } }\n",
        .options = { "-Wall" },
        .warnings = 8,
        .c23 = true,
    },
    {
        // Clang sorts a function body's unused declarations by place as it pops its scope: its labels among its own
        // variables, after an inner block's, before the body's runtime warnings.
        .name = "unused labels among a body's unused variables",
        .source = "void f(int x) { int a; l: x++; { int b; c: ; } int d; m: ; int e; }\n"
                  "void g(int x) { int a; l: { int b; } (void)x; }\n"
                  "void h(int x) { l: { int b; k: ; } int y = 1 << 40; (void)x; }\n"
                  "void s(int x) { lab: ; static int st; (void)x; }\n",
        .options = { "-Wall" },
        .warnings = 17,
    },
    {
        // A function with an error warns of no unused label, as of no unused variable; a pragma ignores one.
        .name = "unused labels beside errors and pragmas",
        .source = "void f(int x) { l: x++; int y = undeclared; }\n"
                  "void g(int x) { l: x++; }\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wunused-label\"\n"
                  "void k(void) { q: ; }\n"
                  "#pragma clang diagnostic pop\n"
                  "void k2(void) { q: ; }\n",
        .options = { "-Wunused-label" },
        .warnings = 2,
        .errors = 1,
    },
    {
        // A goto whose jump into a statement expression is an error still uses its label, which the recovery's view
        // leaves out with the goto; an unused variable of that function still warns, as the jump's error isn't its scope's.
        .name = "a label an erroneous goto names",
        .source = "void f(int x) { int y; goto in; x = ({ in: 1; }); }\n"
                  "void g(void) { out: ; }\n",
        .options = { "-Wall" },
        .warnings = 2,
        .errors = 1,
        .notes = 1,
    },
    {
        // An asm goto's labels are used as a goto's are: the function's, and a local label its block's list declares,
        // not one of the same name in another scope.
        .name = "labels an asm goto names",
        .source = "void f(void) { asm goto(\"\" :::: l2, l4); l2: ; l4: ; l5: ; }\n"
                  "void g(void) { { __label__ l; asm goto(\"\" :::: l); l: ; } }\n"
                  "void h(void) { { __label__ m; m: ; } asm goto(\"\" :::: m); m: ; }\n"
                  "void q(void) { { __label__ z; z: ; } { __label__ z; asm goto(\"\" :::: z); z: ; } }\n",
        .options = { "-Wall" },
        .warnings = 3,
    },
};

static void check_test_driver_unused_labels(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_label_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unused_label_fixtures));
}
