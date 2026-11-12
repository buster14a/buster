// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original diagnose_if and enable_if sources (#1447, c_check_condition.c). Clang supplies the external diagnostic oracle.
// Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both
// kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c. A condition's own
// error drops its attribute and the reader goes on (#2383), as clang's does.

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
    // A volatile parameter's read is never a constant (#2383); each declaration's error drops its attribute, and the unit
    // goes on to the next one's and to its calls.
    {
        .name = "enable_if and diagnose_if reading a volatile parameter",
        .source = "int b7(volatile int a) __attribute__((enable_if(a > 0, \"\")));\n"
                  "int b8(volatile int a) __attribute__((diagnose_if(a > 0, \"x\", \"warning\")));\n"
                  "int g; int c(int x) __attribute__((enable_if(g, \"\")));\n"
                  "int d(int x) __attribute__((enable_if(x, \"\")));\n"
                  "void h(void) { b7(1); c(1); d(0); }\n",
        .errors = 4,
        .notes = 4,
    },
    {
        .name = "a volatile parameter's read that some arguments skip",
        .source = "int c1(int x, volatile int a) __attribute__((enable_if(x && a, \"\")));\n"
                  "int c2(int x, volatile int a) __attribute__((enable_if(x || a, \"\")));\n"
                  "int c3(int x, volatile int a) __attribute__((enable_if(x ? a : 1, \"\")));\n"
                  "int c4(volatile int a) __attribute__((enable_if(sizeof(a) == 4, \"\")));\n"
                  "int c5(volatile int *a) __attribute__((enable_if(a != 0, \"\")));\n"
                  "void g(int* p) { c1(1, 2); c1(0, 2); c2(0, 3); c2(1, 3); c3(0, 1); c4(1); c5(p); }\n",
        .errors = 4,
        .notes = 4,
    },
    // Casts clang's constant evaluation refuses (#2253): to void, its note "non-literal type 'void'", and between an integer
    // and a pointer but a null pointer constant's, "this conversion is not allowed"; where no arguments skip them, in a
    // parameter's condition or not. A void condition isn't one; a call reads past a void left operand.
    {
        .name = "casts to void and between integers and pointers in a condition",
        .source = "void c(int x) __attribute__((diagnose_if(((void)x, x), \"m\", \"warning\")));\n"
                  "void l(long x) __attribute__((diagnose_if(((void*)x, x), \"m\", \"warning\")));\n"
                  "void a1(int x) __attribute__((diagnose_if(((void)0, x), \"m\", \"warning\")));\n"
                  "void a2(int x) __attribute__((diagnose_if((void)x, \"m\", \"warning\")));\n"
                  "void a3(int x) __attribute__((diagnose_if(x && ((void)x, 1), \"m\", \"warning\")));\n"
                  "void a4(int x) __attribute__((diagnose_if(((void*)0, x), \"m\", \"warning\")));\n"
                  "void a8(int x) __attribute__((enable_if(((void)x, x), \"m\")));\n"
                  "void a9(int *x) __attribute__((diagnose_if(((void*)x, 1), \"m\", \"warning\")));\n"
                  "void b1(int x) __attribute__((diagnose_if(((void)0, 1), \"m\", \"warning\")));\n"
                  "void b5(int x) __attribute__((diagnose_if(((void)x, 1) && x, \"m\", \"warning\")));\n"
                  "void b6(int x) __attribute__((diagnose_if(sizeof((void)x, 1) == 4 && x, \"m\", \"warning\")));\n"
                  "void b7(int x) __attribute__((diagnose_if((long)(char*)0 == 0 && x, \"m\", \"warning\")));\n"
                  "void b8(int x) __attribute__((diagnose_if(x ? 1 : ((void)x, 0), \"m\", \"warning\")));\n"
                  "void h(void) { a3(1); a9(0); b6(1); b8(1); b8(0); }\n",
        .warnings = 7,
        .errors = 8,
        .notes = 11,
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
    {
        .name = "object and function addresses are never null",
        .source = "int glob;\n"
                  "void helper(void) {}\n"
                  "void f1(char *p) __attribute__((enable_if(p != 0, \"nonnull\")));\n"
                  "void f3(void (*fp)(void)) __attribute__((enable_if(fp != 0, \"nn\")));\n"
                  "void f4(int *p) __attribute__((enable_if(p != 0, \"nonnull\")));\n"
                  "void d(int *p) __attribute__((diagnose_if(p != 0, \"nonnull arg\", \"warning\")));\n"
                  "void h(void) { int local; f1(\"lit\"); f3(helper); f4(&glob); d(&glob); d(0); f4(&local); }\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "a variable read a short circuit can skip",
        .source = "int g;\n"
                  "void f(int x) __attribute__((enable_if(x > 0 && g > 0, \"c\")));\n"
                  "void k(int x) __attribute__((enable_if(x > 0 ? g : 1, \"c\")));\n"
                  "void m(int x) __attribute__((diagnose_if(x > 0 || g, \"c\", \"warning\")));\n"
                  "void h(void) { f(0); }\n"
                  "void h2(void) { k(0); }\n"
                  "void h3(void) { m(1); m(0); }\n",
        .warnings = 1,
        .errors = 1,
        .notes = 2,
    },
    {
        .name = "addresses compared, tested and cast",
        .source = "int a[4], b;\n"
                  "void f(int *p, int *q) __attribute__((diagnose_if(p == q, \"same\", \"warning\")));\n"
                  "void g(int *p) __attribute__((diagnose_if(!p, \"null\", \"warning\"), diagnose_if(p ? 1 : 0, \"set\", \"warning\")));\n"
                  "void k(const char *s) __attribute__((diagnose_if((const void *)s != 0, \"cast\", \"warning\")));\n"
                  "void h(void) { f(a, &b); f(0, 0); g(a); g(0); k(\"x\"); k(0); }\n",
        .warnings = 4,
        .notes = 4,
    },
    {
        .name = "unknown operands skipped at calls",
        .source = "int g;\n"
                  "void m(int x) __attribute__((diagnose_if(x > 0 || g, \"or\", \"warning\")));\n"
                  "void n(int x, int y) __attribute__((diagnose_if(x == 0 || y > 3, \"either\", \"warning\")));\n"
                  "void k(int x) __attribute__((enable_if(x > 0 ? g : 1, \"c\")));\n"
                  "void h(int v) { m(1); m(0); n(0, v); n(1, v); n(v, 5); k(0); }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "one address error for each operation that converts",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void r(int x) __attribute__((enable_if(x > 0, \"p2\")));\n"
                  "void s(int x);\n"
                  "void g(void (*)(int), void (*)(int));\n"
                  "void f1(int c) { void (*p)(int) = c ? q : r; (void)p; }\n"
                  "void f2(int c) { (void)(c ? r : q); (void)(c ? (q) : (r)); (void)(q ? q : 0); }\n"
                  "void f3(int c) { (void)(q == r); (void)(q, r); (void)(q && r); (void)(q ?: r); }\n"
                  "void f4(int c) { (void)(c ? (c ? q : r) : r); (void)(q == (c ? r : s)); }\n"
                  "void f5(int c) { (void)(&q == &r); (void)(c ? &q : &r); }\n",
        .errors = 14,
    },
    {
        .name = "one address error for each call and statement",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void r(int x) __attribute__((enable_if(x > 0, \"p2\")));\n"
                  "void s(int x);\n"
                  "void g(void (*)(int), void (*)(int));\n"
                  "void f6(int c) { g(q, r); g(s, q); }\n"
                  "void f7(int c) { q; r; }\n"
                  "void f8(int c) { (void)((q == s) + (r == s)); (void)((s == q) == r); }\n"
                  "void f9(int c) { (void)(c ? q : r) == (c ? r : q); }\n",
        .errors = 9,
    },
    {
        .name = "address errors in initializer lists",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void r(int x) __attribute__((enable_if(x > 0, \"p2\")));\n"
                  "void s(int x);\n"
                  "struct P { void (*a)(int); void (*b)(int); };\n"
                  "void f1(int c) { void (*a[2])(int) = { q, r }; (void)a; }\n"
                  "void f2(int c) { void (*a[2])(int) = { q, c ? r : s }; (void)a; }\n"
                  "void f3(int c) { struct P p = { .a = q, .b = c ? r : s }; (void)p; }\n"
                  "void f4(int c) { struct P p[2] = { { q, s }, { c ? s : r, s } }; (void)p; }\n",
        .errors = 5,
    },
    {
        .name = "address errors inside unevaluated operands",
        .source = "void q(int x) __attribute__((enable_if(x > 0, \"p1\")));\n"
                  "void r(int x) __attribute__((enable_if(x > 0, \"p2\")));\n"
                  "void f(int c) { (void)sizeof(q == r); (void)sizeof(&q); (void)sizeof((q)); (void)sizeof q; (void)sizeof(q, r); }\n"
                  "void h(int c) { __typeof__(c ? q : r) *p = 0; (void)p; (void)__alignof__(q); (void)_Generic(q, default: 0); }\n",
        .errors = 5,
    },
    {
        .name = "warning groups",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .warnings = 5,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-format",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-format" },
        .warnings = 5,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wall",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wall" },
        .warnings = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-all",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-all" },
        .warnings = 2,
    },
    {
        .name = "warning groups under -Werror=shadow",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Werror=shadow" },
        .warnings = 4,
        .errors = 2,
        .notes = 1,
    },
    {
        .name = "warning groups under -Werror -Wno-error=shadow",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Werror", "-Wno-error=shadow" },
        .errors = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-error=shadow -Werror",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-error=shadow", "-Werror" },
        .errors = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-everything",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-everything" },
        .warnings = 4,
        .errors = 1,
    },
    {
        .name = "warning groups under -Wfatal-errors=shadow",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wfatal-errors=shadow" },
        .errors = 1,
    },
    {
        .name = "warning groups under -Wno-error=format",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-error=format" },
        .warnings = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wformat",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wformat" },
        .warnings = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-shadow -Wshadow",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-shadow", "-Wshadow" },
        .warnings = 5,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-unused",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-unused" },
        .warnings = 4,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "warning groups under -W",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-W" },
        .warnings = 5,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "warning groups under -Wno-error=format -Werror",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "void u(int x) __attribute__((diagnose_if(x > 0, \"plain\", \"warning\")));\n"
                  "void a(int x) __attribute__((diagnose_if(x > 0, \"most\", \"warning\", \"all\"), diagnose_if(x > 1, \"unused\", \"warning\", \"unused-variable\")));\n"
                  "void p(int x) __attribute__((diagnose_if(x > 0, \"pedantic\", \"warning\", \"pedantic\")));\n"
                  "int m(void) { f(1); h(2); u(1); a(2); p(1); f(0); return 0; }\n",
        .options = { "-Wno-error=format", "-Werror" },
        .errors = 6,
        .notes = 1,
    },
    {
        .name = "warning groups under pragmas",
        .source = "void f(int x) __attribute__((diagnose_if(x > 0, \"shadowed\", \"warning\", \"shadow\")));\n"
                  "void h(int x) __attribute__((diagnose_if(x > 0, \"formatted\", \"error\", \"format\")));\n"
                  "#pragma clang diagnostic warning \"-Wformat\"\n"
                  "int m(void) { f(1); h(2); return 0; }\n"
                  "#pragma clang diagnostic ignored \"-Wshadow-all\"\n"
                  "int n(void) { f(1); h(2); return 0; }\n"
                  "#pragma clang diagnostic error \"-Wshadow\"\n"
                  "#pragma clang diagnostic ignored \"-Wmost\"\n"
                  "int k(void) { f(1); h(2); return 0; }\n",
        .warnings = 3,
        .errors = 1,
    },
    {
        .name = "calls, assignments and increments a call skips or leaves unknown",
        .source = "int g(int);\n"
                  "int h(void);\n"
                  "int abs(int);\n"
                  "void f0(int x) __attribute__((diagnose_if(g(x) > 0, \"c0\", \"warning\")));\n"
                  "void f1(int x) __attribute__((diagnose_if(x > 3 || g(x), \"c1\", \"warning\")));\n"
                  "void f2(int x) __attribute__((diagnose_if(x > 0 && g(1), \"c2\", \"warning\")));\n"
                  "void f3(int x) __attribute__((diagnose_if(x ? g(1) : 1, \"c3\", \"warning\")));\n"
                  "void f4(int x) __attribute__((diagnose_if(g(g(x)), \"c4\", \"warning\")));\n"
                  "void f5(int x) __attribute__((diagnose_if(abs(x) > 3, \"c5\", \"warning\")));\n"
                  "void f6(int x) __attribute__((diagnose_if(x > 0 && (x = 3), \"c6\", \"warning\")));\n"
                  "void f7(int x) __attribute__((diagnose_if(x > 0 || (x = 1), \"c7\", \"warning\")));\n"
                  "void f8(int x) __attribute__((diagnose_if(x > 0 ? x++ : 0, \"c8\", \"warning\")));\n"
                  "void f9(int x) __attribute__((diagnose_if(x > 0 && g(x = 1), \"c9\", \"warning\")));\n"
                  "void k(void) { f0(5); f0(0); f1(5); f1(0); f2(5); f2(0); f3(5); f3(0); f4(5); f4(0); f5(5); f5(0); f6(5); f6(0); f7(5); f7(0); f8(5); f8(0); f9(5); f9(0); }\n",
        .warnings = 3,
        .notes = 3,
    },
    {
        .name = "a condition that never produces a constant: x == g(1)",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(x == g(1), \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: (x += 1) > 2",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if((x += 1) > 2, \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: --x > 2",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(--x > 2, \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: x++ > 3",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(x++ > 3, \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: g(x = 1)",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(g(x = 1), \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: (x++, 1)",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if((x++, 1), \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: h()",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(h(), \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "a condition that never produces a constant: g(1) > 0",
        .source = "int g(int);\nint h(void);\n"
                  "void f(int x) __attribute__((diagnose_if(g(1) > 0, \"c\", \"warning\")));\n",
        .errors = 1,
        .notes = 1,
    },
    // A comma operator's left operand in a condition (#1447, check_input_attribute_unused): -Wunused-value's "has no effect"
    // at the place clang's isUnusedResultAWarning gives (a nested comma's right operand, a conditional's else arm, a logical
    // operator, a cast's `(`, sizeof's keyword), quiet for a call, an assignment and a literal 0 after a comma, and
    // -Wunused-comparison's, with its note, for a comparison; and in another attribute's argument.
    {
        .name = "comma operators' left operands in conditions",
        .source = "void f(int x) __attribute__((diagnose_if((x, x > 1), \"big\", \"warning\")));\n"
                  "void g(int x) __attribute__((enable_if((1, x), \"e\")));\n"
                  "void h(int x) __attribute__((diagnose_if((x + 1, 2, x > 1), \"big\", \"error\")));\n"
                  "void k(void) { f(2); g(1); h(0); }\n",
        .warnings = 5,
        .notes = 1,
    },
    {
        .name = "the shapes of a comma's discarded left operand",
        .source = "void a(int x) __attribute__((diagnose_if((x == 1, x), \"m\", \"warning\")));\n"
                  "void b(int x) __attribute__((diagnose_if((x < 1, x), \"m\", \"warning\")));\n"
                  "void d(int x) __attribute__((diagnose_if((-x, x), \"m\", \"warning\")));\n"
                  "void e(int x) __attribute__((diagnose_if((x ? 1 : 2, x), \"m\", \"warning\")));\n"
                  "void f(int x) __attribute__((diagnose_if((sizeof x, x), \"m\", \"warning\")));\n"
                  "void g(int x) __attribute__((diagnose_if(((long)x, x), \"m\", \"warning\")));\n"
                  "void h(int x) __attribute__((diagnose_if((x && 1, x), \"m\", \"warning\")));\n"
                  "void i(int x) __attribute__((diagnose_if((0, x), \"m\", \"warning\")));\n"
                  "void j(int x) __attribute__((diagnose_if((1, 2), \"m\", \"warning\")));\n"
                  "void k(int x) __attribute__((diagnose_if(((x, 1), x), \"m\", \"warning\")));\n"
                  "void l(int x) __attribute__((diagnose_if(((x, 0), x > 1), \"m\", \"warning\")));\n"
                  "void m(int x) __attribute__((diagnose_if((x != 1, x), \"m\", \"warning\")));\n"
                  "void n(int x) __attribute__((enable_if((x, 1), \"m\")));\n"
                  "void o(int x) __attribute__((diagnose_if((x ? 1 : x++, x), \"m\", \"warning\")));\n",
        .warnings = 14,
        .notes = 2,
    },
    {
        .name = "a comma operator in another attribute's argument",
        .source = "int c __attribute__((aligned((7, 8))));\n",
        .warnings = 1,
        .errors = 1,
    },    // Overloadable functions' conditions in overload resolution (#1447, c_check_overload.c): an enable_if that doesn't
    // admit the call makes its candidate not viable, the candidate whose enable_ifs are another's and more is better, and
    // ones that differ (clang's profile: the tree, literals by value, parameters by place and type) are neither, and
    // are two functions of one signature; diagnose_if applies to the selected candidate. A call no candidate or several are
    // viable for has clang's error and candidate notes. And a pointer variable's argument, which is no constant.
    {
        .name = "overloads disabled by enable_if, and the one selected",
        .source = "int f(int a) __attribute__((overloadable, enable_if(a > 0, \"positive\")));\n"
                  "char f(long a) __attribute__((overloadable));\n"
                  "_Static_assert(sizeof(f(1)) == 4, \"enabled\");\n"
                  "_Static_assert(sizeof(f(-1)) == 1, \"disabled\");\n"
                  "void t(int x) { (void)f(x); }\n",
    },
    {
        .name = "no overload viable: a disabled candidate's note and another's argument count",
        .source = "int g(int a) __attribute__((overloadable, enable_if(a > 0, \"pos\")));\n"
                  "int g(int a, int b) __attribute__((overloadable));\n"
                  "void t(int x) { g(1); g(-1); g(x); g(); g(1, 2, 3); }\n",
        .errors = 4,
        .notes = 8,
    },
    {
        .name = "a variadic overload's count and an empty message",
        .source = "int v(int a, ...) __attribute__((overloadable, enable_if(a > 1, \"\")));\n"
                  "int v(int a, int b, int c) __attribute__((overloadable));\n"
                  "void t(void) { v(); v(2, 3); v(0, 1); }\n",
        .errors = 2,
        .notes = 4,
    },
    {
        .name = "the overload with more enable_if conditions is better",
        .source = "int m(int a, long x) __attribute__((overloadable, enable_if(a > 0, \"a\")));\n"
                  "int m(int a, long x) __attribute__((overloadable, enable_if(a > 0, \"a\")));\n"
                  "char m(int b, long long y) __attribute__((overloadable, enable_if(b > 0x0, \"b\"), enable_if(b < 9, \"c\")));\n"
                  "_Static_assert(sizeof(m(1, 0)) == 1, \"more\");\n"
                  "_Static_assert(sizeof(m(10, 0)) == 4, \"fewer\");\n",
    },
    {
        .name = "enable_if conditions that differ are ambiguous",
        .source = "#define ONE 1\n"
                  "int g1(int a, long x) __attribute__((overloadable, enable_if(a > (1), \"\"), enable_if(a < 9, \"\")));\n"
                  "char g1(int b, long long y) __attribute__((overloadable, enable_if(b > 1, \"\")));\n"
                  "int g2(int a, long x) __attribute__((overloadable, enable_if(a > 1L, \"\"), enable_if(a < 9, \"\")));\n"
                  "char g2(int b, long long y) __attribute__((overloadable, enable_if(b > ONE, \"\")));\n"
                  "int g3(long a) __attribute__((overloadable, enable_if(a > 0, \"\"), enable_if(a < 9, \"\")));\n"
                  "char g3(unsigned long a) __attribute__((overloadable, enable_if(a > 0, \"\")));\n"
                  "int g4(int a) __attribute__((overloadable, enable_if(a > 0, \"\"), enable_if(a < 9, \"\")));\n"
                  "int g4(int a) __attribute__((overloadable, enable_if(a < 9, \"\"), enable_if(a > 0, \"\")));\n"
                  "void t(void) { g1(3, 0); g2(3, 0); g3(1); g4(4); g4(10); }\n",
        .errors = 5,
        .notes = 10,
    },
    {
        .name = "an overload whose enable_if conditions differ is another function",
        .source = "int n(int a) __attribute__((overloadable, enable_if(a > 0, \"a\")));\n"
                  "int n(int a) __attribute__((overloadable));\n"
                  "int n(int a) __attribute__((overloadable, enable_if(a > 0, \"a\")));\n"
                  "int n(int a) __attribute__((overloadable, enable_if(a > 0, \"a\"))) { return a; }\n"
                  "int h(int a) __attribute__((overloadable, enable_if(a > 0, \"a\")));\n"
                  "int h(int a) __attribute__((overloadable, enable_if(a < 10, \"b\")));\n"
                  "void t(void) { n(1); n(-1); h(30); h(-1); h(5); }\n",
        .errors = 1,
        .notes = 2,
    },
    {
        .name = "an ambiguous call's candidates",
        .source = "long w(long a) __attribute__((overloadable));\n"
                  "long w(unsigned long a) __attribute__((overloadable));\n"
                  "long w(long long a) __attribute__((overloadable));\n"
                  "void t(void) { w(1); }\n",
        .errors = 1,
        .notes = 3,
    },
    // An overload call's candidate that fails by an argument's conversion (#2384): clang's "no known conversion" note,
    // these first in display order; and an overload set's address that matches no candidate, '<overloaded function
    // type>' with each candidate's note (check_driver_overload_address).
    {
        .name = "a candidate that fails by an argument's conversion",
        .source = "#define OV __attribute__((overloadable))\n"
                  "struct S { int x; };\n"
                  "int f(int a) OV;\n"
                  "int f(int *a, int b) OV;\n"
                  "void g(struct S s) { f(s); }\n",
        .errors = 1,
        .notes = 2,
    },
    {
        .name = "conversion failures first, with clang's fix-it words",
        .source = "#define OV __attribute__((overloadable))\n"
                  "struct S { int x; };\n"
                  "struct T { int y; };\n"
                  "int f(int a) OV;\n"
                  "int f(struct T t, int b) OV;\n"
                  "int f(int a, int b, int c) OV;\n"
                  "void g(struct S s, long *lp) { f(s); f(s, 1); f(1, s); f(s, s); }\n"
                  "int h(long *a) OV;\n"
                  "int h(struct T t) OV;\n"
                  "void k(int i) { h(i); h(1.5); }\n",
        .errors = 6,
        .notes = 16,
    },
    {
        .name = "an overload set's address that matches no candidate",
        .source = "#define OV __attribute__((overloadable))\n"
                  "int f(int a) OV __attribute__((enable_if(a > 0, \"pos\")));\n"
                  "int f(double a) OV;\n"
                  "int f(int a, int b) OV;\n"
                  "long f(char *s) OV;\n"
                  "int (*p)(int) = f;\n"
                  "void g(void) { int (*q)(long) = f; q = f; (void)q; }\n"
                  "int (*r)(int) = &f;\n",
        .errors = 4,
        .notes = 16,
    },
    {
        // Returned or passed (#2480, check_driver_overload_operand): '<overloaded function type>' as the source, with a
        // parameter's note where the callee's declaration names it, and no candidate notes; and an incompatible pointer
        // argument a viable conversion, whose error names the selected candidate's parameter.
        .name = "an overload set's address returned or passed, and an incompatible pointer argument",
        .source = "#define OV __attribute__((overloadable))\n"
                  "int f(double a) OV;\n"
                  "int f(char a) OV;\n"
                  "void take(int (*)(int));\n"
                  "void take2(int (*q)(int), int);\n"
                  "int (*ret(void))(int) { return f; }\n"
                  "void g(void) { take(f); take2(&f, 1); }\n"
                  "struct T { int y; };\n"
                  "int h(long *a) OV;\n"
                  "int h(struct T t) OV;\n"
                  "void k(int *ip) { h(ip); }\n",
        .errors = 4,
        .notes = 3,
    },
    {
        .name = "an overload set's address with one candidate the enable_ifs leave",
        .source = "long fc(char*);\n"
                  "int (*p)(int) = fc;\n"
                  "#define OV __attribute__((overloadable))\n"
                  "int f(int a) OV __attribute__((enable_if(a > 0, \"pos\")));\n"
                  "int f(double a) OV;\n"
                  "int (*p2)(int) = f;\n"
                  "int g(double a) OV;\n"
                  "int g(char a) OV;\n"
                  "int (*p3)(int) = g;\n"
                  "long h(double a) OV;\n"
                  "long h(char a) OV;\n"
                  "int (*p4)(int) = h;\n",
        .errors = 4,
        .notes = 6,
    },
    {
        .name = "an overload set's address: parameter counts, return types, variadic",
        .source = "#define OV __attribute__((overloadable))\n"
                  "long h(int a) OV;\n"
                  "int h(int a, ...) OV;\n"
                  "int h(int a, int b) OV;\n"
                  "int (*p4)(int) = h;\n"
                  "void (*p5)(int) = h;\n",
        .errors = 2,
        .notes = 6,
    },
    {
        .name = "diagnose_if on the selected overload",
        .source = "int k(int a) __attribute__((overloadable, diagnose_if(1, \"always\", \"warning\"), diagnose_if(a > 1, \"dep\", \"warning\")));\n"
                  "int k(double a) __attribute__((overloadable, diagnose_if(a > 5, \"bigd\", \"error\")));\n"
                  "int (*p)(int) = k;\n"
                  "void t(void) { k(2); k(6.0); k(1.0); int (*q)(double) = k; (void)q; }\n",
        .warnings = 2,
        .errors = 1,
        .notes = 3,
    },
    {
        .name = "diagnose_if accumulates over an overload's redeclarations",
        .source = "int r(int a) __attribute__((overloadable, enable_if(a > 0, \"e\"), diagnose_if(a > 1, \"one\", \"warning\")));\n"
                  "int r(int a) __attribute__((overloadable, enable_if(a > 0, \"e\"), diagnose_if(a > 2, \"two\", \"warning\")));\n"
                  "int r(double a) __attribute__((overloadable));\n"
                  "void t(void) { r(5); r(-5); }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "pointer arguments of enable_if, overloaded or not",
        .source = "int p1(int *p) __attribute__((overloadable, enable_if(p != 0, \"nn\")));\n"
                  "int p1(int *p, int n) __attribute__((overloadable));\n"
                  "int e1(int *p) __attribute__((enable_if(p != 0, \"nn\")));\n"
                  "int x, arr[4]; int *g;\n"
                  "void t(int *q) { p1(&x); p1(arr); p1(0); p1(q); p1(g); p1((int *)0); e1(&x); e1(q); e1(g); }\n",
        .errors = 6,
        .notes = 10,
    },
    // A user's function with a pass_object_size parameter reads its conditions as any other's (#2147): an undeclared name,
    // a constant condition at a call, enable_if's and diagnose_if's at calls, overloads, and its address.
    {
        .name = "pass_object_size functions' undeclared and constant conditions",
        .source = "void *f(void *p __attribute__((pass_object_size(0))), unsigned long n)\n"
                  "    __attribute__((diagnose_if(zz, \"always\", \"warning\")));\n"
                  "void *f2(void *p __attribute__((pass_object_size(0))), unsigned long n)\n"
                  "    __attribute__((diagnose_if(1, \"always\", \"warning\")));\n"
                  "void g(void) { char b[4]; f2(b, 8); }\n",
        .warnings = 1,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "pass_object_size functions' conditions at calls",
        .source = "#define OV __attribute__((overloadable))\n"
                  "void *f(void *p __attribute__((pass_object_size(0))), unsigned long n) __attribute__((enable_if(n < 100, \"small\")));\n"
                  "int g(int *p __attribute__((pass_object_size(1))), int k) OV __attribute__((enable_if(k > 0, \"pos\")));\n"
                  "int g(int *p, int k) OV;\n"
                  "int h(char *p __attribute__((pass_object_size(0))), int k) __attribute__((diagnose_if(k == 3, \"three\", \"warning\"), diagnose_if(k == 4, \"four\", \"error\")));\n"
                  "void t(void) { char b[4]; int i[2]; f(b, 8); f(b, 200); g(i, 1); g(i, 0); h(b, 3); h(b, 4); h(b, 5); (void)&f; }\n",
        .warnings = 1,
        .errors = 3,
        .notes = 3,
    },
    {
        .name = "pass_object_size functions' conditions that hold",
        .source = "void *f(void *p __attribute__((pass_object_size(0))), unsigned long n) __attribute__((enable_if(n < 100, \"small\")));\n"
                  "char gb[4];\n"
                  "int h(char *p __attribute__((pass_object_size(0))), int k) __attribute__((enable_if(k == 3, \"three\")));\n"
                  "int h2(int k, char *p __attribute__((pass_object_size(0)))) __attribute__((enable_if(k == 3, \"three\")));\n"
                  "void t(char *q) { char b[4]; f(0, 8); f(gb, 8); f(&gb[1], 8); f(q, 8); h(b, 3); h(gb, 3); h2(3, b); }\n",
    },
};

static void check_test_driver_conditions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_condition_fixtures, BUSTER_ARRAY_LENGTH(check_driver_condition_fixtures));
}
