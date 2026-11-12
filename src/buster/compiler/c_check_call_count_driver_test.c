// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Call argument counts and calls without a prototype (#1860, #1921, #1994), and a call's failed argument conversions (#2413): original fixtures whose whole diagnostic
// output is compared with clang's on both paths, at each warning policy, with -fsyntax-only and -c, on one and three
// lanes. Include after c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static const CheckDriverDefaultWarningFixture check_driver_call_count_fixtures[] =
{
    {
        // An ordinary call's count error (#1921): clang's "too few/too many arguments to function call" with the counts, too
        // many at the first extra argument, and the "declared here" note where the callee is a function's name, through
        // parentheses and `&`, a typedef's function too; none for a pointer's or a member's.
        .name = "ordinary calls' argument counts",
        .source = "int h(void);\n"
                  "int k(int, int);\n"
                  "int v(int, ...);\n"
                  "typedef int F(int x);\n"
                  "F m;\n"
                  "int g(void)\n"
                  "{\n"
                  "    int (*fp)(int) = 0;\n"
                  "    struct { int (*f)(int y); } s = { 0 };\n"
                  "    h(1);\n"
                  "    k(1);\n"
                  "    k(1, h(), 4, 5);\n"
                  "    v();\n"
                  "    m();\n"
                  "    m(1, 2);\n"
                  "    fp();\n"
                  "    fp(1, 2);\n"
                  "    s.f();\n"
                  "    (h)(1);\n"
                  "    (&k)(1);\n"
                  "    return 0;\n"
                  "}\n",
        .warnings = 0,
        .errors = 11,
        .notes = 8,
    },
    {
        // A callee whose declaration names its only parameter: clang's "single argument 'a'" words, the latest declaration's
        // name, and a variadic one's "at least argument"; an unnamed one keeps the counts.
        .name = "single named parameter",
        .source = "int h(int a);\n"
                  "int h(int b);\n"
                  "int k(int a, ...);\n"
                  "int u(int);\n"
                  "int g(void)\n"
                  "{\n"
                  "    h(1, 2);\n"
                  "    h();\n"
                  "    k();\n"
                  "    u();\n"
                  "    u(1, 2, 3);\n"
                  "    (h)();\n"
                  "    (&h)();\n"
                  "    return 0;\n"
                  "}\n",
        .warnings = 0,
        .errors = 7,
        .notes = 7,
    },
    {
        // Builtins' count errors (#1860): each family's count, exact or a bound as clang's checkArgCountRange and
        // checkArgCountAtLeast word it, too many at the first extra argument but "at most" at the `)`, a scoped atomic's
        // count with its scope, and va_start's operands.
        .name = "builtin argument counts",
        .source = "typedef float v4 __attribute__((vector_size(16)));\n"
                  "void f(int x, ...)\n"
                  "{\n"
                  "    int i = 0;\n"
                  "    double d = 1;\n"
                  "    int *p = &i;\n"
                  "    v4 v = { 0 };\n"
                  "    __builtin_va_list ap;\n"
                  "    __builtin_constant_p();\n"
                  "    __builtin_constant_p(1, 2);\n"
                  "    __builtin_object_size(p);\n"
                  "    __builtin_object_size(p, 0, 1);\n"
                  "    __sync_fetch_and_add(p);\n"
                  "    __atomic_fetch_add(p, 1);\n"
                  "    __atomic_fetch_add(p, 1, 0, 0);\n"
                  "    __builtin_isnan(d, d);\n"
                  "    __builtin_copysign(d, d, d);\n"
                  "    __builtin_flt_rounds(1);\n"
                  "    __builtin_shufflevector(v);\n"
                  "    __c11_atomic_load(p);\n"
                  "    __c11_atomic_load(p, 0, 0);\n"
                  "    __atomic_load(p, p);\n"
                  "    __atomic_load(p, p, 0, 0);\n"
                  "    __hip_atomic_load();\n"
                  "    __hip_atomic_load(p, 0);\n"
                  "    __atomic_is_lock_free(4);\n"
                  "    __atomic_is_lock_free(4, p, 1);\n"
                  "    __atomic_load_n(p);\n"
                  "    __atomic_load_n(p, 0, 1);\n"
                  "    __atomic_thread_fence();\n"
                  "    __builtin_fpclassify(1, 2, 3, 4, 5);\n"
                  "    __builtin_fpclassify(1, 2, 3, 4, 5, d, 7);\n"
                  "    __builtin_inf(1);\n"
                  "    __builtin_va_start();\n"
                  "    __builtin_va_start(ap, x, __builtin_popcount(2));\n"
                  "    __builtin_va_end();\n"
                  "    __builtin_va_copy(ap);\n"
                  "    __builtin_expect(1);\n"
                  "    __builtin_expect(1, 2, 3);\n"
                  "    __builtin_assume_aligned(p);\n"
                  "    __builtin_assume_aligned(p, 1, 2, 3);\n"
                  "    __builtin_prefetch();\n"
                  "    __builtin_prefetch(p, 1, 2, 3);\n"
                  "    __builtin_unreachable(1);\n"
                  "    __builtin_clzg();\n"
                  "    __builtin_clzg(1u, 2, 3);\n"
                  "    __builtin_classify_type();\n"
                  "    __builtin_isgreater(d);\n"
                  "    __builtin_isfpclass(d);\n"
                  "    __builtin_nan();\n"
                  "    __builtin_elementwise_abs();\n"
                  "    __builtin_reduce_add();\n"
                  "    __builtin_dump_struct(p);\n"
                  "    __builtin_complex(d, d, d);\n"
                  "}\n",
        .options = { "-ferror-limit=0" },
        .warnings = 0,
        .errors = 46,
        .notes = 0,
    },
    {
        // The unary, overflow and complex part builtins, whose wrong count the reader reports (#1860).
        .name = "unary and overflow builtin counts",
        .source = "void f(int *p, double d)\n"
                  "{\n"
                  "    __builtin_popcount();\n"
                  "    __builtin_popcount(1, 2);\n"
                  "    __builtin_ctz();\n"
                  "    __builtin_bswap32(1, __builtin_popcount(2));\n"
                  "    __builtin_add_overflow(1, 2);\n"
                  "    __builtin_add_overflow(1, 2, p, 4);\n"
                  "    __builtin_sadd_overflow(1, 2);\n"
                  "    __builtin_creal();\n"
                  "    __builtin_creal(d, d);\n"
                  "    __builtin_cimagf(1, 2, 3);\n"
                  "}\n",
        .warnings = 0,
        .errors = 10,
        .notes = 0,
    },
    {
        // Passing arguments without a prototype (#1994): at the call's `(`, naming a direct callee, "a function" for a
        // pointer; a K&R definition's too few and too many at the call's `)`.
        .name = "calls without a prototype",
        .source = "void knr();\n"
                  "void (*fp)();\n"
                  "struct S { void (*g)(); } s;\n"
                  "void kd(a, b) int a, b; {}\n"
                  "void f(void)\n"
                  "{\n"
                  "    knr(1);\n"
                  "    fp(1);\n"
                  "    (knr)(1);\n"
                  "    (&knr)(1);\n"
                  "    s.g(1);\n"
                  "    (*fp)(2);\n"
                  "    kd(1);\n"
                  "    kd(1, 2, 3);\n"
                  "    (kd)(1);\n"
                  "    knr();\n"
                  "}\n",
        .warnings = 10,
        .errors = 0,
        .notes = 0,
    },
    {
        // In C23 `()` is a prototype (#1994): the count error and its note.
        .name = "C23 empty parameter lists",
        .source = "void knr();\n"
                  "void f(void) { knr(1); knr(1, 2); (knr)(3); }\n",
        .warnings = 0,
        .errors = 3,
        .notes = 3,
        .c23 = true,
    },
    {
        // A call the constant reader checks (a typeof's, an array bound's, an unevaluated sizeof's operand): the same words,
        // its named only parameter's too (check_constant_failure_error).
        .name = "a constant reader's variadic call",
        .source = "int k(int b, ...);\n"
                  "void f(void)\n"
                  "{\n"
                  "    typeof(k()) u;\n"
                  "    (void)u;\n"
                  "}\n",
        .warnings = 0,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "an array bound's call",
        .source = "int h(int a);\n"
                  "int arr[sizeof(h())];\n",
        .warnings = 0,
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "an unevaluated operand's call",
        .source = "int h(int a);\n"
                  "void f(void)\n"
                  "{\n"
                  "    (void)sizeof(h(1, 2));\n"
                  "}\n",
        .warnings = 0,
        .errors = 1,
        .notes = 1,
    },
    {
        // A call's later arguments that don't convert either (#2413, check_graph_report_arguments): each its own error at its
        // argument, with its parameter's note, after an error-by-default conversion (incompatible pointers, an integer and a
        // pointer); a hard error (incompatible types, another address space) is the call's last.
        .name = "two failed argument conversions",
        .source = "void g(int *p, int (*fp)(int));\n"
                  "void f(long *l) { g(l, l); }\n",
        .errors = 2,
        .notes = 2,
    },
    {
        .name = "failed argument conversions until a hard error",
        .source = "struct S { int a; };\n"
                  "void g(int *p, int (*fp)(int), int x, struct S s, char *c);\n"
                  "void f(long *l, struct S s, unsigned char *u) { g(l, l, 1, s, (char*)u); g(0, 0, s, 1, 0); g(l, 0, 0, s, (char*)0); }\n"
                  "void h(void (*fp)(int *, int *), long *l) { fp(l, l); (*fp)(l, 0); }\n"
                  "void k(int __seg_gs *a, int *b);\n"
                  "void m(int *p, int __seg_fs *q) { k(p, q); k(q, q); }\n",
        .errors = 9,
        .notes = 6,
    },
    {
        // A call's conversion warnings and errors in argument order (#2481): an earlier argument's error before a later
        // one's -Wpointer-sign, both after the warnings of an inner call its arguments build.
        .name = "argument conversions in order",
        .source = "struct S { int a; };\n"
                  "void g(int *p, int (*fp)(int), int x, struct S s, char *c);\n"
                  "void f(long *l, struct S s, unsigned char *u) { g(l, 0, 1, s, u); }\n"
                  "void h(int a, char *c);\n"
                  "void f2(unsigned char *u) { h(u, u); }\n"
                  "void k(int *p, char *c, struct S s);\n"
                  "int m(char *c);\n"
                  "void f3(long *l, unsigned char *u, struct S s) { k(l, u, 1); k(l, (char *)l, s); m(l); int x = m(u) + m(l); (void)x; }\n",
        .warnings = 4,
        .errors = 7,
        .notes = 11,
    },
};

static void check_test_driver_call_counts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_call_count_fixtures, BUSTER_ARRAY_LENGTH(check_driver_call_count_fixtures));
}
