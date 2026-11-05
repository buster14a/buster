// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original GNU noderef fixtures, compared with the pinned Clang oracle. Each body contains at most one warned
// dereference: Clang's hash-set order for several delayed warnings is itself unstable between invocations. Complete
// diagnostic lines still compare locations, names, declaration notes, suppression and conversion ordering exactly.

static void check_test_driver_noderef(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char prefix[] = "typedef int __attribute__((noderef)) *NP;\n";
    static const char* const units[] = {
        "void f(NP p) { (void)*p; }\n",
        "int __attribute__((noderef)) scalar;\n",
        "int *__attribute__((noderef)) p; void f(void) { (void)*p; }\n",
        "int __attribute__((noderef)) a[3]; void f(void) { (void)a[0]; }\n",
        "int *p __attribute__((noderef)); void f(void) { (void)*p; }\n",
        "typedef int __attribute__((noderef)) Scalar; Scalar *p; void f(void) { (void)*p; }\n",
        "void f(int *__attribute__((noderef)) *p) { (void)**p; }\n",
        "void f(int __attribute__((noderef)) **p) { (void)**p; }\n",
        "typedef NP Alias; void f(Alias p) { (void)*p; }\n",
        "NP make(void); void f(void) { (void)*make(); }\n",
        "typedef NP (*Factory)(void); void f(Factory p) { (void)*p(); }\n",
        "void f(int __attribute__((noderef)) p[3]) { (void)p[0]; }\n",
        "struct S { NP p; }; void f(struct S *s) { (void)*s->p; }\n",
        "struct S { NP p[3]; }; void f(struct S *s) { (void)*s->p[1]; }\n",
        "struct S { int n; }; void f(struct S __attribute__((noderef)) *s) { (void)s->n; }\n",
        "struct S { int n; }; void f(struct S __attribute__((noderef)) *s) { (void)(*s).n; }\n",
        "struct S { int n; }; void f(struct S __attribute__((noderef)) *s) { (void)&s->n; }\n",
        "struct S { int a[3]; }; void f(struct S __attribute__((noderef)) *s) { (void)&s->a[1]; }\n",
        "struct S { int *p; }; void f(struct S __attribute__((noderef)) *s) { (void)&s->p[1]; }\n",
        "struct S { struct S *next; int n; }; void f(struct S __attribute__((noderef)) *s) { (void)&s->next->n; }\n",
        "void f(NP p) { (void)&*p; }\n",
        "void f(NP p) { (void)&p[1]; }\n",
        "void f(NP p) { (void)*&p; }\n",
        "void f(NP p) { (void)sizeof *p; }\n",
        "void f(NP p) { (void)__alignof__(*p); }\n",
        "void f(NP p) { __typeof__(*p) n = 1; (void)n; }\n",
        "void f(NP p) { (void)_Generic(*p, int: 1); }\n",
        "void f(NP p) { (void)__builtin_choose_expr(0, *p, 1); }\n",
        "void f(NP p) { (void)(0 ? *p : 1); }\n",
        "void f(NP p) { (void)*(int *)p; }\n",
        "void f(int *p) { (void)*(NP)p; }\n",
        "void f(NP p, int *q, int n) { (void)*(n ? p : q); }\n",
        "void f(NP p, int *q, int n) { (void)*(n ? q : p); }\n",
        "void f(NP p, NP q, int n) { (void)*(n ? p : q); }\n",
        "void f(NP p, NP q) { (void)*(p, q); }\n",
        "void f(NP p, int n) { (void)*(p + n); }\n",
        "void f(NP p, int n) { (void)*(n + p); }\n",
        "void f(NP p) { (void)*p++; }\n",
        "void f(NP p) { (void)*++p; }\n",
        "void f(NP p) { int *q = p; (void)q; }\n",
        "void f(NP p, int *q) { q = p; }\n",
        "int *f(NP p) { return p; }\n",
        "void g(int *); void f(NP p) { g(p); }\n",
        "void f(NP p, int *q) { (void)*(p = q); }\n",
        "void g(NP); void f(int *p) { g(p); }\n",
        "void f(NP p) { __auto_type q = p; (void)*q; }\n",
        "void f(NP p) { __typeof__(p) q = p; (void)*q; }\n",
        "int *g(void); NP g(void); void f(void) { (void)*g(); }\n",
        "NP g(void); int *g(void); void f(void) { (void)*g(); }\n",
        "extern int *p; extern NP p; void f(void) { (void)*p; }\n",
        "extern NP p; extern int *p; void f(void) { (void)*p; }\n",
        "void f(int *p); void f(NP p) { (void)*p; }\n",
        "void f(NP p); void f(int *p) { (void)*p; }\n",
        "void f(int __attribute__((__noderef__)) *p) { (void)*p; }\n",
        "int __attribute__((noderef(unknown))) *p;\n",
        "[[clang::noderef]] int *p; void f(void) { (void)*p; }\n",
        "void f(NP p) { struct S { NP p; } s = {p}; (void)*s.p; }\n",
        "int __attribute__((noderef)) *a, *b; void f(void) { (void)*b; }\n",
        "void f(NP p) { (void)*(&*p); }\n",
        "void f(int __attribute__((noderef)) **p) { (void)&**p; }\n",
        "void f(int *__attribute__((noderef)) *p) { (void)&**p; }\n",
        "int __attribute__((noderef)) (*p)(void);\n",
        "#pragma clang diagnostic ignored \"-Wnoderef\"\nvoid f(NP p) { (void)*p; }\n",
        "void f(NP p) { int *a[] = {p}; (void)a; }\n",
        "void f(NP p) { NP a[] = {p}; (void)a; }\n",
        "void f(NP p) { int *a[2][2] = {[1][0] = p}; (void)a; }\n",
        "void f(NP p) { NP a[2][2] = {[1][0] = p}; (void)a; }\n",
        "struct S { int *p; }; void f(NP p) { struct S s = {p}; (void)s; }\n",
        "struct S { NP p; }; void f(NP p) { struct S s = {.p = p}; (void)s; }\n",
        "union U { NP marked; int *plain; }; void f(NP p) { union U u = {.plain = p}; (void)u; }\n",
        "union U { int *plain; NP marked; }; void f(NP p) { union U u = {.marked = p}; (void)u; }\n",
        "struct S { union { NP marked; int *plain; }; }; void f(NP p) { struct S s = {.plain = p}; (void)s; }\n",
        "struct S { NP p[2]; }; void f(NP p) { struct S s = {.p[1] = p}; (void)s; }\n",
        "void f(NP p) { int *a[3] = {[0 ... 2] = p}; (void)a; }\n",
        "void f(NP p) { (void)(int *[]){p}; }\n",
        "void f(NP p) { (void)(NP[]){p}; }\n",
        "void f(NP p) { (void)sizeof(int *[]){p}; }\n",
        "void f(NP p) { int *q = (p); (void)q; }\n",
        "void f(NP p) { _Atomic(int *) a[1] = {p}; (void)a; }\n",
        "void f(NP p) { _Atomic(NP) a[1] = {p}; (void)a; }\n",
        "void f(_Atomic(NP) p) { (void)*p; }\n",
        "void g(_Atomic(NP)); void f(NP p) { g(p); }\n",
        "#if __STDC_VERSION__ < 202311L\nvoid f(p) NP p; { (void)*p; }\n#endif\n",
        "#if __STDC_VERSION__ < 202311L\nvoid f(q,p) NP (*q)(int v), p; { (void)*p; }\n#endif\n",
        "#if __STDC_VERSION__ < 202311L\nvoid f(p) int __attribute__((noderef)) p[2]; { (void)p[0]; }\n#endif\n",
        "int __attribute__((noderef(1))) *p;\n",
        "void f(NP p, int *q) { (void)sizeof(q = p); }\n",
        "void g(int *); void f(NP p) { (void)__alignof__(g(p)); }\n",
        "void f(NP p) { int *a[2][2] = {[0 ... 1][0 ... 1] = p}; (void)a; }\n",
        "void g(NP, int *, NP, int *); void f(NP p) { g(p, p, p, p); }\n",
        "void g(void); void f(NP p) { ((void (*)(NP, int *, NP, int *))g)(p, p, p, p); }\n",
        "void f(NP p) { int *q = (p + 1); (void)q; }\n",
        "void f(NP p) { int *q = p + 1; (void)q; }\n",
        "void f(NP p, int *q) { q = ((p)); }\n",
        "int *f(NP p) { return ((p)); }\n",
        "void g(int *); void f(NP p) { g(((p))); }\n",
        "struct S { NP p; }; void f(struct S s) { int *q = s.p; (void)q; }\n",
        "void f(NP p) { _Atomic(int *) q = p; (void)q; }\n",
        "void g(_Atomic(int *)); void f(NP p) { g(p); }\n",
        "void f(_Atomic(NP) p) { int *q = p; (void)q; }\n",
        "void g(int *); void f(_Atomic(NP) p) { g(p); }\n",
        "void f(void) { int __attribute__((noderef)) a[2]; int *q = a; (void)q; }\n",
        "void g(int *); void f(void) { int __attribute__((noderef)) a[2]; g(a); }\n",
        "void g(int, ...); void f(NP p) { g(0, p); }\n",
        "int (__attribute__((noderef)) *p); void f(void) { (void)*p; }\n",
        "typedef int (__attribute__((noderef)) I); void f(I *p) { (void)*p; }\n",
        "int (*(__attribute__((noderef)) p)); void f(void) { (void)*p; }\n",
        "int (__attribute__((noderef)) *p)[3]; void f(void) { (void)*p; }\n",
        "void f(int *p) { (void)*(int (__attribute__((noderef)) *))p; }\n",
        "void f(void) { (void)sizeof(int (__attribute__((noderef)) *)); }\n",
        "void g(int (__attribute__((noderef)) *)); void f(NP p) { g(p); }\n",
        "void f(int (__attribute__((noderef)) *p)) { (void)*p; }\n",
        "struct S { int (__attribute__((noderef)) *p); }; void f(struct S s) { (void)*s.p; }\n",
        "void (__attribute__((noderef)) f)(NP p) { (void)*p; }\n",
        "_Atomic(NP) p; void f(void) { (void)*p; }\n",
        "void f(_Atomic(NP) p) { (void)p[0]; }\n",
        "int (__attribute__((noderef)) *p)[3]; void f(void) { (void)p[0]; }\n",
        "int (__attribute__((noderef)) *p)[3]; void f(void) { (void)p[0][0]; }\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/noderef.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        char text[4096];
        const int length = snprintf(text, sizeof(text), "%s%s", prefix, units[unit]);
        const bool made = length > 0 && (uint32_t)length < sizeof(text) && parse_test_write(source, text, (uint64_t)length);
        for (uint32_t standard = 0; made && standard < 2; standard += 1)
        {
            for (uint32_t errors = 0; errors < 2; errors += 1)
            {
                char* arguments[16] = { (char*)clang, standard != 0 ? "-std=gnu23" : "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics",
                                       "-Wno-unused-value", source, errors != 0 ? "-Werror=noderef" : NULL, NULL };
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                char description[4096];
                snprintf(description, sizeof(description), "noderef %u: clang's diagnostic lines read", unit);
                check_test_expect(test, lines, description);
                for (uint32_t mode = 0; lines && mode < (errors != 0 ? 1u : 3u); mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t count = errors != 0 ? 7 : 6;
                    if (mode == 1)
                    {
                        arguments[count++] = "--scalar";
                    }
                    else if (mode == 2)
                    {
                        arguments[count++] = "--deferred-bodies";
                        arguments[count++] = "--lanes";
                        arguments[count++] = "2";
                        arguments[count++] = "--poison-arenas";
                    }
                    arguments[count] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    snprintf(description, sizeof(description), "noderef %u (%s), %s mode %u error %u: status %d (clang %d), lines\n%s(clang\n%s)",
                             unit, units[unit], arguments[1], mode, errors, status, oracle, actual_lines, expected_lines);
                    const bool matched = same && (status != 0) == (oracle != 0);
                    if (!matched && test->failures >= 24)
                    {
                        // A source has eight mode/standard comparisons. Keep later distinct cases visible even if
                        // earlier ones have already filled the shared reporter's first 24 failure descriptions.
                        fprintf(stderr, "test-checker: %s\n", description);
                    }
                    check_test_expect(test, matched, description);
                    free(actual);
                }
                free(expected);
            }
        }
        check_test_expect(test, made, "write noderef fixture");
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
