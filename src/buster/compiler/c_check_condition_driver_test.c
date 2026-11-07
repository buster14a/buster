// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original diagnose_if and enable_if sources (#1447, c_check_condition.c). Clang supplies the external diagnostic oracle.
// Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both
// kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c. Each source has at
// most one error the reader finds before its calls are checked: the driver gives a unit's first such error alone.

static const CheckDriverDefaultWarningFixture check_driver_condition_fixtures[] =
{
    {
        .name = "the address of an enable_if function in parentheses",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void f(void) { void (*p)(int) = (q); (void)p; }\n",
        .errors = 1,
    },
    {
        .name = "unevaluated uses and calls through parentheses",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void f(void) { (void)sizeof(q); (void)_Alignof(q); __typeof__(q) *z = 0; (void)z; (q)(5); (&q)(5); }\n",
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "a call no enable_if admits",
        .source = "void f(int x) __attribute__((enable_if(x > 0, \"positive\")));\n"
                  "int h(void) { f(1); f(0); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a disabled call gives no diagnose_if",
        .source = "void u(int x) __attribute__((enable_if(x, \"nz\"), diagnose_if(x == 3, \"three\", \"warning\")));\n"
                  "void f(void) { u(3); u(0); }\n",
        .warnings = 1,
        .errors = 1,
        .notes = 2,
    },
    {
        .name = "enable_if that is always false",
        .source = "void z(int x) __attribute__((enable_if(0, \"never\")));\n"
                  "int h(void) { z(1); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "enable_if with a message that is no string",
        .source = "void f(int x) __attribute__((enable_if(x, 1)));\n",
        .errors = 1,
    },
    {
        .name = "enable_if that never produces a constant",
        .source = "int g; void f(int x) __attribute__((enable_if(g, \"a\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "enable_if with an argument that is no constant",
        .source = "void f(int x) __attribute__((enable_if(x > 0, \"positive\")));\n"
                  "int h(int n) { f(n); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "the candidate note at the latest declaration",
        .source = "void r(int x) __attribute__((enable_if(x > 0, \"a\")));\n"
                  "void r(int x);\n"
                  "int h(void) { r(0); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "enable_if with one argument",
        .source = "void f(int x) __attribute__((enable_if(x)));\n",
        .errors = 1,
    },
    {
        .name = "enable_if of the latest declaration first",
        .source = "void q2(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void q2(int x) __attribute__((enable_if(x > 1, \"p2\")));\n"
                  "int h(void) { q2(5); q2(1); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "enable_if of a declaration's first",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\"), enable_if(x > 1, \"p2\")));\n"
                  "int h(void) { q(0); return 0; }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "enable_if with three arguments",
        .source = "void f(int x) __attribute__((enable_if(x, \"a\", \"b\")));\n",
        .errors = 1,
    },
    {
        .name = "the first error alone at a call",
        .source = "void n(int x) __attribute__((diagnose_if(x > 0, \"w\", \"warning\"), diagnose_if(x > 0, \"e1\", \"error\"), diagnose_if(x > 0, \"e2\", \"error\")));\n"
                  "void h(void) { n(0); n(1); }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "an error at a use",
        .source = "void b(void) __attribute__((diagnose_if(1, \"e\", \"error\")));\n"
                  "void c(void) { b(); }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "too many arguments",
        .source = "void f2(int x) __attribute__((diagnose_if(x, \"m\", \"warning\", \"foo\", \"bar\")));\n",
        .errors = 1,
    },
    {
        .name = "a warning group that is no string",
        .source = "void f5(int x) __attribute__((diagnose_if(x, \"m\", \"warning\", 1)));\n",
        .errors = 1,
    },
    {
        .name = "an unknown warning group",
        .source = "void f1(int x) __attribute__((diagnose_if(x, \"m\", \"warning\", \"foo\")));\n",
        .errors = 1,
    },
    {
        .name = "an empty warning group",
        .source = "void f6(int x) __attribute__((diagnose_if(x, \"m\", \"warning\", \"\")));\n",
        .errors = 1,
    },
    {
        .name = "a diagnostic type that is no string",
        .source = "void f6(int x) __attribute__((diagnose_if(x, \"m\", warning)));\n",
        .errors = 1,
    },
    {
        .name = "an invalid diagnostic type",
        .source = "void f2(int x) __attribute__((diagnose_if(x, \"m\", \"info\")));\n",
        .errors = 1,
    },
    {
        .name = "a message that is no string",
        .source = "void f3(int x) __attribute__((diagnose_if(x, 5, \"warning\")));\n",
        .errors = 1,
    },
    {
        .name = "a condition that never produces a constant",
        .source = "int g; void f1(int x) __attribute__((diagnose_if(g, \"glob\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a dependent condition that reads a variable",
        .source = "int g; void f1(int x) __attribute__((diagnose_if(x > g, \"glob\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "one argument",
        .source = "void f3(int x) __attribute__((diagnose_if(x)));\n",
        .errors = 1,
    },
    {
        .name = "too few arguments",
        .source = "void f4(int x) __attribute__((diagnose_if(x, \"m\")));\n",
        .errors = 1,
    },
    {
        .name = "an undeclared name",
        .source = "void f7(int x) __attribute__((diagnose_if(y, \"m\", \"warning\")));\n",
        .errors = 1,
    },
    {
        .name = "no argument",
        .source = "void f4(int x) __attribute__((diagnose_if()));\n",
        .errors = 1,
    },
    {
        .name = "a tautological enable_if",
        .source = "void k(int x) __attribute__((enable_if(1, \"always\")));\n"
                  "void f(void) { void (*p)(int) = k; k(2); (void)p; }\n",
    },
    {
        .name = "a system header's enable_if(1, "")",
        .source = "# 1 \"sys.h\" 1 3\n"
                  "int w(int n) __attribute__((enable_if(1, \"\")));\n"
                  "# 3 \"user.c\" 2\n"
                  "int main(void){ int (*p)(int) = w; return w(1) + p(2); }\n",
    },
    {
        .name = "a system header function",
        .source = "# 1 \"sys.h\" 1 3\n"
                  "static inline int header_wrapper(int n) __attribute__((diagnose_if((n>8),(\"too large\"),\"warning\"))){return n;}\n"
                  "int opaque(char *p, int n) __attribute__((diagnose_if(__builtin_object_size(p, 1) < n, \"small\", \"warning\")));\n"
                  "# 4 \"user.c\" 2\n"
                  "int main(void){ char b[4]; return header_wrapper(3) + header_wrapper(9) + opaque(b, 2); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "an array parameter",
        .source = "void f(int a[4], int n) __attribute__((diagnose_if(n > 4, \"over\", \"warning\")));\n"
                  "void g(int *p) { f(p, 5); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "a _Bool parameter",
        .source = "void f(_Bool b) __attribute__((diagnose_if(b, \"true\", \"warning\")));\n"
                  "void g(void){ f(5); f(0); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "the C23 spelling",
        .source = "[[clang::diagnose_if(1, \"c23\", \"warning\")]] void a(void);\n"
                  "void f(void) { a(); }\n",
        .warnings = 1,
    },
    {
        .name = "a character parameter",
        .source = "void f(char c) __attribute__((diagnose_if(c == 'x', \"x\", \"warning\")));\n"
                  "void g(void){ f('x'); f(120); f(1); }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "a message of joined strings",
        .source = "void f8(int x) __attribute__((diagnose_if(x, \"m\" \"n\", \"warning\"))); void g(void){f8(1);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "a definition with diagnose_if",
        .source = "int d(int x) __attribute__((diagnose_if(x == 4, \"def\", \"warning\"))) { return x; }\n"
                  "int f(void) { return d(4) + d(3); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "enable_if and diagnose_if together",
        .source = "void u(int x) __attribute__((enable_if(x, \"nz\"), diagnose_if(x == 3, \"three\", \"warning\")));\n"
                  "void f(void) { u(3); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "an enumerator in a condition",
        .source = "enum { LIMIT = 8 }; void f(int x) __attribute__((diagnose_if(x > LIMIT, \"big\", \"warning\"))); void g(void){ f(9); f(8); f(LIMIT + 1); }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "a use at file scope",
        .source = "void a(void) __attribute__((diagnose_if(1, \"always\", \"warning\")));\n"
                  "void (*p)(void) = a;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "a long long parameter",
        .source = "void f(long long x) __attribute__((diagnose_if(x > 4000000000LL, \"huge\", \"warning\")));\n"
                  "void g(void){ f(5000000000LL); f(1); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "shifts that fault decide nothing",
        .source = "void f(int x) __attribute__((diagnose_if((1 << x) > 100, \"shift\", \"warning\")));\n"
                  "void g(void){ f(10); f(40); f(-1); f(2); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "nested arithmetic",
        .source = "void f(int a, int b) __attribute__((diagnose_if((a + b) * 2 > 10 || (a ^ b) == 7, \"mix\", \"warning\")));\n"
                  "void g(void){ f(3, 3); f(3, 4); f(1, 1); f(-5, 2); }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "source order, uses of a function and unevaluated uses",
        .source = "void m(int x) __attribute__((diagnose_if(x > 0, \"w1\", \"warning\"), diagnose_if(x > 1, \"w2\", \"warning\")));\n"
                  "void m(int x) __attribute__((diagnose_if(x > 2, \"w3\", \"warning\")));\n"
                  "void r(void) __attribute__((diagnose_if(1, \"u1\", \"warning\"), diagnose_if(1, \"u2\", \"warning\")));\n"
                  "void a(void) __attribute__((diagnose_if(1, \"always\", \"warning\")));\n"
                  "void a(void) { a(); }\n"
                  "int h(void) { m(3); m(1); r(); void (*w)(void) = r; (void)w; (void)sizeof(a); __typeof__(a) *p = 0; (void)p; (void)_Generic(a, default: 1); return 0; }\n",
        .warnings = 12,
        .notes = 12,
    },
    {
        .name = "an overflowing condition decides nothing",
        .source = "void f(int x) __attribute__((diagnose_if(x + 1 > 0, \"ovf\", \"warning\")));\n"
                  "void g(void) { f(2147483647); f(1); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "an unnamed parameter before a named one",
        .source = "void f(int, int y) __attribute__((diagnose_if(y > 1, \"second\", \"warning\")));\n"
                  "void g(void) { f(5, 2); f(5, 0); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "a pragma ignores the warning",
        .source = "#pragma clang diagnostic ignored \"-Wuser-defined-warnings\"\n"
                  "void f(int x) __attribute__((diagnose_if(x > 1, \"big\", \"warning\")));\n"
                  "void g(void){ f(2); }\n",
    },
    {
        .name = "a redeclaration inherits and renames",
        .source = "void f8(int x);\n"
                  "void f8(int y) __attribute__((diagnose_if(y == 3, \"redecl\", \"warning\")));\n"
                  "void f8(int z);\n"
                  "void h(void){ f8(3); f8(2); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer, floating, sizeof, unsigned, conditional, cast and short-circuit conditions",
        .source = "void f9(int *p) __attribute__((diagnose_if(p == 0, \"null\", \"warning\")));\n"
                  "void f10(double d) __attribute__((diagnose_if(d > 1.5, \"dbl\", \"warning\")));\n"
                  "void f11(int x) __attribute__((diagnose_if(sizeof(x) == 4 && x == 1, \"sz\", \"warning\")));\n"
                  "void f12(unsigned x) __attribute__((diagnose_if(x > -1, \"u\", \"warning\")));\n"
                  "void f14(int x) __attribute__((diagnose_if(x ? x / 2 == 3 : -x == 1, \"cond\", \"warning\")));\n"
                  "void f15(int x) __attribute__((diagnose_if((unsigned char)x == 1, \"cast\", \"warning\")));\n"
                  "void f16(int x) __attribute__((diagnose_if(x != 0 && 10 / x == 5, \"div\", \"warning\")));\n"
                  "void f17(char *p) __attribute__((diagnose_if(!p, \"notp\", \"warning\")));\n"
                  "void h(int n){ f9(0); f9(&n); f10(2.0); f10(1); f11(1); f12(5); f14(6); f14(-1); f14(2); f15(257); f15(2); f16(0); f16(2); f17((void*)0); f17(0); }\n",
        .warnings = 8,
        .notes = 8,
    },
    {
        .name = "diagnose_if on a variable",
        .source = "int v __attribute__((diagnose_if(1, \"var\", \"warning\")));\n",
        .warnings = 1,
    },
    {
        .name = "a variadic function",
        .source = "void v(int x, ...) __attribute__((diagnose_if(x > 2, \"var\", \"warning\"))); void c(void) { v(3); v(3, 1, 2); v(1, 5); }\n",
        .warnings = 2,
        .notes = 2,
    },
};

static void check_test_driver_conditions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_condition_fixtures, BUSTER_ARRAY_LENGTH(check_driver_condition_fixtures));
}
