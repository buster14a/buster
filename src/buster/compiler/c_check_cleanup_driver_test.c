// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's cleanup attribute's errors (#147) by the driver against clang's, on original fixtures: every error, its place and
// its message, in order, by both paths of -fsyntax-only. test-checker's fixtures (c_check_cleanup_test.c) compare where
// each source is first rejected; these tell the messages apart. Two differences are taken: clang names the two types of
// its parameter's error, where the driver says what clang's other spelling of the message does ("parameter has
// incompatible type"); and after a syntax error in a declaration (a type's name as the argument), clang's later errors
// in it, which the parser skips with the rest of the declaration. A cleanup's error, as the reader's errors but an
// undeclared name's, ends the unit's checking (check_input_diagnostic_recover), so each fixture has one, or undeclared
// names. The notes clang attaches to the jumps' errors aren't compared. Include after c_checker_driver_recovery_test.c.

// The errors of a diagnostic text, each "file:line:column: error: message" and a newline, at most `limit` of them (all for
// 0), with clang's "parameter has type 'P' which is incompatible with type 'V'" as "parameter has incompatible type".
static bool check_test_cleanup_errors(const char* text, uint32_t limit, char* errors, uint64_t capacity)
{
    static const char typed[] = " parameter has type '";
    static const char untyped[] = " parameter has incompatible type";
    uint64_t used = 0;
    uint32_t taken = 0;
    bool valid = text != NULL && capacity != 0;
    for (const char* at = text; valid && at != NULL && *at != 0 && (limit == 0 || taken < limit);)
    {
        const char* end = strchr(at, '\n');
        const uint64_t length = end != NULL ? (uint64_t)(end - at) : strlen(at);
        if (memmem(at, length, ": error: ", 9) != NULL)
        {
            const char* type = memmem(at, length, typed, sizeof(typed) - 1);
            const uint64_t kept = type != NULL ? (uint64_t)(type - at) : length;
            const uint64_t added = type != NULL ? sizeof(untyped) - 1 : 0;
            valid = kept + added + 1 < capacity - used;
            if (valid)
            {
                memcpy(errors + used, at, kept);
                memcpy(errors + used + kept, untyped, added);
                used += kept + added;
                errors[used++] = '\n';
            }
            taken += 1;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    if (capacity != 0)
    {
        errors[used] = 0;
    }
    return valid;
}

static void check_test_driver_cleanup_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        // The errors compared, all for 0.
        uint32_t limit;
    } fixtures[] = {
        { "an undeclared function", "void g(void) {\nint a __attribute__((cleanup(missing)));\n}\n", 0 },
        { "an undeclared name on a file-scope variable", "int a\n__attribute__((cleanup(missing)));\n", 0 },
        { "an undeclared name on an ignored parameter", "void g(int a __attribute__((cleanup(missing)))) { }\n", 0 },
        { "an undeclared name on an ignored field", "struct S { int a __attribute__((cleanup(missing))); };\n", 0 },
        { "an earlier cleanup's undeclared name", "void f(int *);\nvoid g(void) { int a __attribute__((cleanup(missing), cleanup(f))); }\n", 0 },
        { "an undeclared name in parentheses", "void g(void) {\nint a __attribute__((cleanup((missing))));\n}\n", 0 },
        { "its own variable", "void g(void) {\nint a __attribute__((cleanup(a)));\n}\n", 0 },
        { "an object", "int f;\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function pointer", "void f(int *p);\nvoid (*h)(int *) = f;\nvoid g(void) {\nint a __attribute__((cleanup(h)));\n}\n", 0 },
        { "an enumerator", "enum { E };\nvoid g(void) {\nint a __attribute__((cleanup(E)));\n}\n", 0 },
        { "a function in parentheses", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup((f))));\n}\n", 0 },
        { "an object in two parentheses", "int o;\nvoid g(void) {\nint a __attribute__((cleanup(((o)))));\n}\n", 0 },
        { "a string", "void g(void) {\nint a __attribute__((cleanup(\"f\")));\n}\n", 0 },
        { "a number", "void g(void) {\nint a __attribute__((cleanup(1)));\n}\n", 0 },
        { "a character", "void g(void) {\nint a __attribute__((cleanup('f')));\n}\n", 0 },
        { "a literal on a file-scope variable, ignored", "int a __attribute__((cleanup(1)));\nvoid g(void) {\nint b __attribute__((cleanup(b)));\n}\n", 0 },
        { "a type, a syntax error", "typedef int T;\nvoid g(void) {\nint a __attribute__((cleanup(T)));\n}\n", 1 },
        { "an earlier cleanup's type, a syntax error", "typedef int T;\nvoid f(int *);\nvoid g(void) { int a __attribute__((cleanup(T), cleanup(f))); }\n", 1 },
        { "no argument", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup));\n}\n", 0 },
        { "empty parentheses", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup()));\n}\n", 0 },
        { "two arguments", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(f, f)));\n}\n", 0 },
        { "two arguments on a file-scope variable, ignored", "void f(int *p);\nint a __attribute__((cleanup(f, f)));\nvoid g(void) {\nint b __attribute__((cleanup()));\n}\n", 0 },
        { "two arguments before one", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(f, f), cleanup(f)));\n}\n", 0 },
        // A specifier's, which a function declarator before the variables ignores (#745's review): clang reports the count
        // at each variable, at the same place, which the driver prints once.
        { "two arguments on a function, then on a variable", "void f(int *p);\nvoid g(void) {\n__attribute__((cleanup(f, f))) int h(void), a;\n}\n", 0 },
        { "two arguments on a function, then on two variables", "void f(int *p);\nvoid g(void) {\n__attribute__((cleanup(f, f))) int h(void), a, b;\n}\n", 1 },
        { "another pointee after a function", "void f(long *p);\nvoid g(void) {\n__attribute__((cleanup(f))) int h(void), a;\n}\n", 0 },
        { "a function without a prototype", "void f();\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function of two parameters", "void f(int *p, int q);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function of no parameter", "void f(void);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "another pointee", "void f(long *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "another signedness", "void f(int *p);\nvoid g(void) {\nunsigned a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a character pointer", "void f(char *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a pointer to a pointer to void", "void f(void **p);\nvoid g(void) {\nint *a __attribute__((cleanup(f)));\n}\n", 0 },
        { "an integer parameter", "void f(int p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "an element's pointer", "void f(int *p);\nvoid g(int n) {\nint a[n] __attribute__((cleanup(f)));\n}\n", 0 },
        { "another struct", "struct S { int x; };\nstruct T { int x; };\nvoid f(struct T *p);\nvoid g(void) {\nstruct S s __attribute__((cleanup(f)));\n}\n", 0 },
        // An __auto_type variable's, against the deduced type (#745's review), reported where the attribute names the
        // function.
        { "an __auto_type double by an int pointer", "void f(int *p);\nvoid g(void) {\n__auto_type a __attribute__((cleanup(f))) =\n    1.0;\n}\n", 0 },
        { "an __auto_type int by a double pointer, the specifiers'", "void f(double *p);\nvoid g(void) {\n__attribute__((cleanup(f))) __auto_type a =\n    1;\n}\n", 0 },
        { "undeclared names of several declarations", "void g(void) {\nint a __attribute__((cleanup(missing)));\nint b __attribute__((cleanup(other)));\n}\n"
                                                      "void h(void) {\nint c __attribute__((cleanup((gone))));\n}\n", 0 },
        { "a goto into its scope", "void f(int *p);\nvoid g(int n) {\ngoto l;\nint a __attribute__((cleanup(f)));\nl:;\n}\n", 0 },
        { "a goto into a nested scope", "void f(int *p);\nvoid g(int n) {\ngoto l;\n{ int a __attribute__((cleanup(f))); l:; }\n}\n", 0 },
        { "a switch into its scope", "void f(int *p);\nvoid g(int n) { switch (n) {\nint a __attribute__((cleanup(f)));\ncase 1:;\n} }\n", 0 },
        { "a computed goto out of its scope", "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\ngoto *t[0]; }\nl:;\n}\n", 0 },
        { "a computed goto into its scope", "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\nl:; }\ngoto *t[0];\n}\n", 0 },
        // An asm goto's own labels (#1126's review): out of a cleanup's scope, into one and into an array's.
        { "an asm goto out of its scope", "void f(int *p);\nint g(void) { { int a __attribute__((cleanup(f))) = 7;\n  asm goto(\"jmp %l0\" : : : : out); }\nout: return 1; }\n", 0 },
        { "an asm goto into its scope", "void f(int *p);\nint g(void) {\n  asm goto(\"jmp %l0\" : : : : in);\n{ int a __attribute__((cleanup(f))) = 7;\nin: return a; } }\n", 0 },
        { "an asm goto into an array's scope", "int g(int n) {\n  asm goto(\"jmp %l0\" : : : : in);\n{ int a[n]; a[0] = 1;\nin: return a[0]; } }\n", 0 },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/cleanup-errors.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, fixtures[i].limit, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "cleanup errors, %s: clang rejects it", fixtures[i].name);
        check_test_expect(test, made && oracle == 1 && expected_valid && expected[0] != 0, description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, fixtures[i].limit, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "cleanup errors, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
}

// Cleanups' shared chains (#1370): each exit runs one copy of each cleanup's call, the one at its scope's end, and goes
// on to its own destination. A program of every kind of path through a chain (a goto back before a declaration of its
// own block, which stops in the middle of a scope's run; exits through three scopes with a fall at each; a continue, a
// break and a fall at one record, and a switch's break; statement expressions' exits through inner, own and outer
// records, their values kept before mutating cleanups and earlier operands restored only on a fall; nested regions
// that stop inside an outer region then fall through its cleanup, and unevaluated regions with a shared destination;
// target clones that share a cleanup plan while their bodies lower on separate lanes;
// kept, named and void returns; arrays of runtime length whose cleanups read them before an exit restores their stack;
// a for statement's own variables; and a cleanup that longjmps out, then the same chain's fall) and the issue's shapes
// of many cleanups, each followed by an exit, run against clang -O0's build by Tier 0 and Tier 1, on both paths. The
// shapes include a statement expression's inner and own records, and regions leaving enclosing records. Their objects
// at 300 and 600 cleanups take linear room: past 85, the original run at each exit exhausted the call rows.
static const char check_test_cleanup_chain_paths[] =
    "#include <setjmp.h>\n"
    "#include <stdio.h>\n"
    "static unsigned trace;\n"
    "static void note(int *p) { trace = trace * 31u + (unsigned)*p; }\n"
    "static void replace(int *p) { note(p); *p = -500; }\n"
    "static int pair(int a, int b) { return 37 * a + b; }\n"
    "static int midrun(int n) { int r = 0; int k = 0; { int a __attribute__((cleanup(note))) = 1;\n"
    "again:; int b __attribute__((cleanup(note))) = 10 + k; int c __attribute__((cleanup(note))) = 20 + k; k++; if (k < n) goto again; r += b + c; } return r; }\n"
    "static int three(int n) { int r = 0; { int a __attribute__((cleanup(note))) = 1; { int b __attribute__((cleanup(note))) = 2; { int c __attribute__((cleanup(note))) = 3;\n"
    "if (n == 0) return r; if (n == 1) goto out; r += c; } if (n == 2) return r + 1; r += b; } if (n == 3) goto out; r += a; }\n"
    "out: { int d __attribute__((cleanup(note))) = 4; if (n == 4) return 9; } return r; }\n"
    "static int loops(int n) { int r = 0; for (int i = 0; i < n; i++) { int a __attribute__((cleanup(note))) = i; if (i % 3 == 1) continue; if (i == 7) break; r += a; }\n"
    "int j = 0; while (1) { int b __attribute__((cleanup(note))) = 100 + j; j++; if (j > n) break; if (j & 1) continue; r += b; }\n"
    "switch (n) { case 1: { int s __attribute__((cleanup(note))) = 50; if (r) break; r += 2; } case 2: { int s2 __attribute__((cleanup(note))) = 51; break; } default: r += 1; }\n"
    "return r; }\n"
    "static int expressions(int n) { int r = 0; for (int i = 0; i < 4; i++) { int a __attribute__((cleanup(note))) = i;\n"
    "r += ({ int b __attribute__((cleanup(note))) = 10 + i; if (n == i) break; if (n == i + 1) continue; if (n == 9 && i == 2) goto out; if (n == 8 && i == 3) return 77; b; }); }\n"
    "out: return r; }\n"
    "static int regions(int k) { int s = 0; for (int i = 0; i < 4; i++) {\n"
    "s += ({ int r = 0; { int v1 __attribute__((cleanup(note))) = 1; { int v2 __attribute__((cleanup(note))) = 2; if (k == i) break;\n"
    "{ int v3 __attribute__((cleanup(note))) = 3; if (k == i + 1) continue; if (k == 7) goto out; r = v3; }\n"
    "{ int v4 __attribute__((cleanup(note))) = 4; if (k == i + 2) goto inner; r += v4; } } inner: r += 1; } r; }); }\n"
    "out: s += ({ int v9 __attribute__((cleanup(note))) = 9; int t = 0; do { int v5 __attribute__((cleanup(note))) = 5;\n"
    "{ int v6 __attribute__((cleanup(note))) = 6; if (k & 1) break; t += v6; } { int v7 __attribute__((cleanup(note))) = 7; if (k & 2) continue; t += v7; } } while (0); t; });\n"
    "return s; }\n"
    "static int region_values(int n) { int outer __attribute__((cleanup(note))) = 40 + n; int r = 5;\n"
    "r += pair(50 + n, ({ int v __attribute__((cleanup(replace))) = 60 + n; if (n == 0) return v; if (n == 1) goto out; v; }));\n"
    "out: return r; }\n"
    "static int region_back(int n) { return pair(10 + n, ({ int a __attribute__((cleanup(note))) = 20 + n; int k = 0;\n"
    "again:; int b __attribute__((cleanup(replace))) = ++k; if (k < n) goto again; b; })); }\n"
    "static int nested_regions(int n) { int r = 0; for (int i = 0; i < 4; i++) { int a __attribute__((cleanup(note))) = 70 + i;\n"
    "r += pair(2 + i, ({ int b __attribute__((cleanup(replace))) = 80 + i; int v = 0;\n"
    "v += ({ int c __attribute__((cleanup(replace))) = 90 + i; if (n == i) goto resumed; if (n == i + 1) continue;\n"
    "if (n == 8 && i == 2) goto out; if (n == 9 && i == 1) return c; c; }); resumed:; b + v; })); } out: return r; }\n"
    "static void absent_regions(int n) { int a __attribute__((cleanup(note))) = 100 + n; (void)(0 && ({ return; 1; }));\n"
    "(void)(0 ? ({ return; 1; }) : ({ if (n & 1) return; 2; })); note(&n); }\n"
    "__attribute__((target_clones(\"default\", \"sse4.2\", \"avx2\"))) static int cloned_regions(int n) { int a __attribute__((cleanup(note))) = 180 + n;\n"
    "return ({ int b __attribute__((cleanup(replace))) = 190 + n; if (n & 1) return b; b; }); }\n"
    "struct S { int a, b; };\n"
    "static void sn(struct S *p) { trace = trace * 31u + (unsigned)p->a; p->a = -5; }\n"
    "static int region_aggregate(int n) { int r = 7; struct S s = ({ struct S v __attribute__((cleanup(sn))) = { 110 + n, 120 + n };\n"
    "if (n == 2) goto out; v; }); r += s.a + 3 * s.b; out: return r; }\n"
    "static struct S region_return(int n) { int outer __attribute__((cleanup(note))) = 130 + n;\n"
    "struct S r = ({ struct S v __attribute__((cleanup(sn))) = { 140 + n, 150 + n }; if (n == 3) return v; v; }); return (struct S){ r.a + 1, r.b }; }\n"
    "static struct S named(int n) { int x __attribute__((cleanup(note))) = n; if (n == 1) { struct S s __attribute__((cleanup(sn))) = { 1, 2 }; if (n) return s; }\n"
    "struct S t __attribute__((cleanup(sn))) = { 3, n }; if (n == 2) return t; { int y __attribute__((cleanup(note))) = 9; if (n == 3) return t; } return t; }\n"
    "static int kept(int n) { int x __attribute__((cleanup(note))) = n; { int y __attribute__((cleanup(note))) = n + 1; if (n == 1) return x + y; if (n == 2) return y; }\n"
    "if (n == 3) return 4; return x; }\n"
    "static void none(int n) { int x __attribute__((cleanup(note))) = n; if (n == 1) return; { int y __attribute__((cleanup(note))) = n + 1; if (n == 2) return; } }\n"
    "static void array(int (*p)[]) { trace = trace * 31u + (unsigned)(*p)[0] + (unsigned)(*p)[1]; }\n"
    "static int lengths(int n) { int r = 0; for (int i = 0; i < 5; i++) { int v[n + 2] __attribute__((cleanup(array))); v[0] = i; v[1] = n;\n"
    "{ int w __attribute__((cleanup(note))) = i; if (i == n) break; if (i == 1) continue; r += v[0]; } } return r; }\n"
    "static int region_lengths(int n) { int r = 0; for (int i = 0; i < 4; i++) { int outside[n + 2] __attribute__((cleanup(array)));\n"
    "outside[0] = n; outside[1] = 160 + i; r += ({ int inside[n + 3] __attribute__((cleanup(array))); inside[0] = i; inside[1] = 170 + n;\n"
    "if (n == i) break; if (n == i + 1) continue; if (n == 8 && i == 2) goto out; if (n == 9 && i == 2) return inside[0] + outside[0];\n"
    "inside[0] + outside[0]; }); } out: return r; }\n"
    "static int owned(int n) { int r = 0; for (int a __attribute__((cleanup(note))) = 1, b __attribute__((cleanup(note))) = 2; a < 6; a++) {\n"
    "int c __attribute__((cleanup(note))) = a; if (a == n) break; if (a == n + 2) return r; r += c; } return r + 100; }\n"
    "static jmp_buf jumps; static int jumpy;\n"
    "static void leave(int *p) { trace = trace * 31u + (unsigned)*p; if (jumpy) { jumpy = 0; longjmp(jumps, 1); } }\n"
    "static int jumped(int n) { int r = 0; { int a __attribute__((cleanup(leave))) = 1; { int b __attribute__((cleanup(note))) = 2; if (n == 1) return 3; r += b; }\n"
    "if (n == 2) return 4; r += a; } return r; }\n"
    "int main(void) {\n"
    "    for (int n = 0; n < 10; n++) {\n"
    "        trace = 0; int r = midrun(n) + 3 * three(n) + 5 * loops(n) + 7 * expressions(n) + 11 * kept(n) + 13 * lengths(n) + 17 * owned(n) + 23 * regions(n);\n"
    "        r += 29 * region_values(n); r += 31 * nested_regions(n); r += 37 * region_aggregate(n); r += 41 * region_lengths(n); absent_regions(n);\n"
    "        r += 47 * cloned_regions(n);\n"
    "        r += 53 * region_back(n);\n"
    "        struct S value = region_return(n); r += value.a + 43 * value.b;\n"
    "        struct S s = named(n); none(n); jumpy = 1; if (setjmp(jumps) == 0) { jumped(n); } r += 19 * jumped(n);\n"
    "        printf(\"%d %d %d %d %u\\n\", n, r, s.a, s.b, trace);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// The issue's shapes (#1370) of `count` cleanups, each followed by an exit, including a statement expression's inner and
// own records and expressions leaving enclosing records, and a main that runs each over every exit.
static bool check_test_cleanup_chain_shapes(char* text, size_t capacity, uint32_t count)
{
    size_t used = 0;
    bool valid = true;
#define BUSTER_CHECK_TEST_CHAIN_PRINT(...) do { const int written = valid ? snprintf(text + used, capacity - used, __VA_ARGS__) : 0; valid = valid && written >= 0 && (size_t)written < capacity - used; used += valid ? (size_t)written : 0; } while (0)
    BUSTER_CHECK_TEST_CHAIN_PRINT("#include <stdio.h>\nstatic unsigned trace;\nstatic void rel(int *p) { trace = trace * 131u + (unsigned)*p; }\n");
    BUSTER_CHECK_TEST_CHAIN_PRINT("static int returns(int a) { int s = 0;\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u; if (a == %u) return s; s += c%u;\n", i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("return s; }\nstatic int nested(int a) { int s = 0;\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("{ int c%u __attribute__((cleanup(rel))) = a + %u; if (a == %u) return s; s += c%u;\n", i, i, i, i);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("}");
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("\nreturn s; }\nstatic int loop(int a) { int s = 0; for (int k = 0; k < 3; k++) {\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u + k; if (a == %u + k) continue; if (a == %u + 2 * k) break; s += c%u;\n", i, i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("} return s; }\nstatic int jumps(int a) { int s = 0; {\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u; if (a == %u) goto fail; s += c%u;\n", i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("} return s; fail: return -s; }\nstatic int region(int a) { return ({ int s = 0; {\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u; if (a == %u) goto done; s += c%u;\n", i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("} done:; s; }); }\n");
    BUSTER_CHECK_TEST_CHAIN_PRINT("static int region_own(int a) { return ({ int s = 0;\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u; if (a == %u) return s; s += c%u;\n", i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("s; }); }\nstatic int region_outer(int a) { int s = 0;\n");
    for (uint32_t i = 0; i < count; i += 1)
    {
        BUSTER_CHECK_TEST_CHAIN_PRINT("int c%u __attribute__((cleanup(rel))) = a + %u; s += ({ if (a == %u) return s; c%u; });\n", i, i, i, i);
    }
    BUSTER_CHECK_TEST_CHAIN_PRINT("return s; }\n");
    BUSTER_CHECK_TEST_CHAIN_PRINT("int main(void) { unsigned h = 0; for (int a = -1; a < %u; a++) { trace = 0; int r = returns(a) + 3 * nested(a) + 5 * loop(a) + 7 * jumps(a) + 11 * region(a);\n"
                                  "r += 13 * region_own(a); r += 17 * region_outer(a);\n"
                                  "h = h * 31u + (unsigned)r + trace; } printf(\"%%u\\n\", h); return 0; }\n", count + 3);
#undef BUSTER_CHECK_TEST_CHAIN_PRINT
    return valid;
}

static void check_test_driver_cleanup_chains(CheckTest* test, const char* clang, const char* driver, const char* directory)
{
    const char* const executable = driver != NULL ? driver : "/proc/self/exe";
    static char shapes[1 << 20];
    static const char* const names[2] = { "paths", "shapes" };
    static const char* const ways[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    for (uint32_t program = 0; program < 2; program += 1)
    {
        const bool generated = program == 0 || check_test_cleanup_chain_shapes(shapes, sizeof(shapes), 100);
        const char* text = program == 0 ? check_test_cleanup_chain_paths : shapes;
        char source[512];
        char outputs[5][512];
        snprintf(source, sizeof(source), "%s/cleanup-chains-%s.c", directory, names[program]);
        for (uint32_t k = 0; k < 5; k += 1)
        {
            snprintf(outputs[k], sizeof(outputs[k]), "%s/cleanup-chains-%s-%u", directory, names[program], k);
        }
        const bool written = generated && parse_test_write(source, text, strlen(text));
        char* output = NULL;
        char* const reference[] = { (char*)clang, "-O0", "-w", "-o", outputs[4], source, NULL };
        const int built = written ? check_test_driver_spawn(reference, &output) : 1;
        free(output);
        output = NULL;
        static char expected[4096];
        const bool referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
        char description[256];
        snprintf(description, sizeof(description), "cleanup chains (%s): clang's build runs", names[program]);
        check_test_expect(test, referenced && strlen(expected) != 0, description);
        for (uint32_t mode = 0; referenced && mode < 4; mode += 1)
        {
            char* arguments[16] = { (char*)executable };
            uint32_t count = 1;
            if ((mode & 1) != 0)
            {
                arguments[count++] = "--scalar";
            }
            if (mode >= 2)
            {
                arguments[count++] = "--tier1";
                arguments[count++] = "--verify";
            }
            if (program == 0)
            {
                arguments[count++] = "--lanes";
                arguments[count++] = "3";
            }
            arguments[count++] = "-o";
            arguments[count++] = outputs[mode];
            arguments[count++] = source;
            arguments[count] = NULL;
            const int status = check_test_driver_spawn(arguments, &output);
            static char captured[4096];
            const bool ran = status == 0 && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
            snprintf(description, sizeof(description), "cleanup chains (%s): each cleanup runs once on every path, as clang's build's do (%s)", names[program], ways[mode]);
            check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
            if (status != 0 && output != NULL)
            {
                fprintf(stderr, "%s", output);
            }
            else if (ran && strcmp(captured, expected) != 0)
            {
                fprintf(stderr, "cleanup chains (%s, %s):\n%s\nclang's build:\n%s\n", names[program], ways[mode], captured, expected);
            }
            free(output);
            output = NULL;
            unlink(outputs[mode]);
        }
        unlink(outputs[4]);
        unlink(source);
    }
    // Linear room: the shapes' object at 600 cleanups is about twice the one at 300 (the runs made it about four times).
    int64_t sizes[2] = { 0, 0 };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        char source[512];
        char object[512];
        snprintf(source, sizeof(source), "%s/cleanup-chains-room-%u.c", directory, k);
        snprintf(object, sizeof(object), "%s/cleanup-chains-room-%u.o", directory, k);
        const bool written = check_test_cleanup_chain_shapes(shapes, sizeof(shapes), 300 * (k + 1)) && parse_test_write(source, shapes, strlen(shapes));
        char* output = NULL;
        char* arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
        const int status = written ? check_test_driver_spawn(arguments, &output) : 1;
        struct stat built = { 0 };
        sizes[k] = status == 0 && stat(object, &built) == 0 ? (int64_t)built.st_size : 0;
        if (status != 0 && output != NULL)
        {
            fprintf(stderr, "%s", output);
        }
        free(output);
        unlink(object);
        unlink(source);
    }
    check_test_expect(test, sizes[0] != 0 && sizes[1] != 0 && sizes[1] * 2 < sizes[0] * 5,
                      "cleanup chains: 300 and 600 cleanups, each with an exit, compile, and the object grows linearly");
}
