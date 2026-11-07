// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A failed assignment conversion's error by the driver against clang (#147, #1447), whole diagnostic output on both paths:
// clang's DiagnoseAssignmentResult words of an initializer's (at its declarator, or at a braced subobject's value), an
// assignment's (at its ), a return's (at its expression) and an argument's (at the argument, with its parameter's note)
// conversion, by its kind (integer and pointer, -Wint-conversion; pointers, -Wincompatible-pointer-types; function
// pointers; an incompatible type), with the types as written and clang's fix-it words; a record's and an array's whole
// initializer of the wrong value; and a static object's second, non-constant error. Include after
// c_check_label_attribute_driver_test.c (check_test_diagnostic_lines).

static void check_test_driver_conversion_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    typedef struct CheckConversionUnit CheckConversionUnit;
    struct CheckConversionUnit
    {
        const char* source;
        const char* standard;
    };
    static const CheckConversionUnit units[] = {
        { "void f(void) { int *p = 0; int x = p; }\n", "-std=c17" },
        { "void f(void) { static int a; int x = &a; }\n", "-std=c17" },
        { "void f(void) { int i = 0; int *q = i; }\n", "-std=c17" },
        { "void f(void) { int **pp = 0; int *q = pp; }\n", "-std=c17" },
        { "void f(void) { int *p = 0; int **pp = p; }\n", "-std=c17" },
        { "void f(void) { int *ip = 0; int *q = *ip; }\n", "-std=c17" },
        { "void f(void) { const int *p = 0; int x = p; }\n", "-std=c17" },
        { "void f(void) { int *p = 0; const int x = p; }\n", "-std=c17" },
        { "void f(void) { int *p = 0; int x; x = p; }\n", "-std=c17" },
        { "int f(int *p) { return p; }\n", "-std=c17" },
        { "void g(int); void f(int *p) { g(p); }\n", "-std=c17" },
        { "void f(void) { int *p = 0; long x = (p); }\n", "-std=c17" },
        { "void f(void) { int *p = 0; int x = (p); }\n", "-std=c17" },
        { "void f(void) { int x = (int *)0; }\n", "-std=c17" },
        { "void f(void) { int a[2]; int x = a; }\n", "-std=c17" },
        { "void f(void) { char *p = 0; unsigned char x = p; }\n", "-std=c17" },
        { "typedef int T; void f(void) { T *p = 0; int x = p; }\n", "-std=c17" },
        { "void f(void) { int i = 0; int *q = (i); }\n", "-std=c17" },
        { "void f(void) { int *p = 0; int *q = &p; }\n", "-std=c17" },
        { "void f(void) { float *p = 0; int *q = p; }\n", "-std=c17" },
        { "void g(int *); void f(float *p) { g(p); }\n", "-std=c17" },
        { "void g(int *q); void f(float *p) { g(p); }\n", "-std=c17" },
        { "void g(int *); void f(int i) { g(i); }\n", "-std=c17" },
        { "struct s { int a; }; void g(int); void f(struct s v) { g(v); }\n", "-std=c17" },
        { "int *f(float *p) { return p; }\n", "-std=c17" },
        { "int *f(int i) { return i; }\n", "-std=c17" },
        { "struct s { int a; }; int f(struct s v) { return v; }\n", "-std=c17" },
        { "void f(int *p, float *q) { p = q; }\n", "-std=c17" },
        { "void f(int *p, int i) { p = i; }\n", "-std=c17" },
        { "void f(int i, int *p) { i = p; }\n", "-std=c17" },
        { "struct s { int a; }; void f(int i, struct s v) { i = v; }\n", "-std=c17" },
        { "void f(int i, double d) { int *p = d; }\n", "-std=c17" },
        { "void g(const char *); void f(int *p) { g(p); }\n", "-std=c17" },
        { "void g(char *); void f(const char *p) { g(p); }\n", "-std=c17" },
        { "void g(unsigned *); void f(int *p) { g(p); }\n", "-std=c17" },
        { "void f(void) { int a[2]; int x; x = a; }\n", "-std=gnu17" },
        { "void g(int); void f(void) { int a[2]; g(a); }\n", "-std=gnu17" },
        { "int f(void) { static int a[2]; return a; }\n", "-std=gnu17" },
        { "void h(void); void f(void) { int x = h; }\n", "-std=gnu17" },
        { "void h(void); void f(void) { int x; x = h; }\n", "-std=gnu17" },
        { "void f(void) { const int c = 1; int *p; p = c; }\n", "-std=gnu17" },
        { "void f(const int *q) { int x; x = q; }\n", "-std=gnu17" },
        { "struct s { int a; }; void f(struct s *p) { struct s v = p; }\n", "-std=gnu17" },
        { "struct s { int a; }; void f(struct s *p) { struct s v; v = p; }\n", "-std=gnu17" },
        { "void f(int (*fp)(void), void (*gp)(void)) { fp = gp; }\n", "-std=gnu17" },
        { "void f(int (*fp)(void), int *q) { fp = q; }\n", "-std=gnu17" },
        { "void f(void *v) { int x; x = v; }\n", "-std=gnu17" },
        { "void f(int *p) { void *v; int **pp; pp = p; }\n", "-std=gnu17" },
        { "typedef int *IP; IP f(double d) { return d; }\n", "-std=gnu17" },
        { "typedef int *IP; void f(IP p) { int x; x = p; }\n", "-std=gnu17" },
        { "void f(int *p) { double d; d = p; }\n", "-std=gnu17" },
        { "void f(int i) { int *p; p = i + 1; }\n", "-std=gnu17" },
        { "struct s { int a; } g(void); void f(void) { int x; x = g(); }\n", "-std=gnu17" },
        { "void f(int *p) { int x; x = (p); }\n", "-std=gnu17" },
        { "void f(int **pp) { int *q; q = *pp + 0; int x; x = *pp; }\n", "-std=gnu17" },
        { "struct s { int a; }; void f(int i) { struct s v = i; }\n", "-std=gnu17" },
        { "struct s { int a; }; struct t { int b; }; void f(struct t w) { struct s v = w; }\n", "-std=gnu17" },
        { "union u { int a; }; void f(int *p) { union u v = p; }\n", "-std=gnu17" },
        { "struct s { int a; }; void f(struct s *p) { struct s v = { p }; }\n", "-std=gnu17" },
        { "struct s { int *a; }; void f(int i) { struct s v = { i }; }\n", "-std=gnu17" },
        { "void f(int *p) { int a[2] = { p }; }\n", "-std=gnu17" },
        { "struct s { int a; } g; void f(int *p) { g = p; }\n", "-std=gnu17" },
        { "void f(int *p) { int a[2] = p; }\n", "-std=gnu17" },
        { "void f(int i) { int a[2] = i; }\n", "-std=gnu17" },
        { "struct s { int a; }; void f(void) { struct s v = \"x\"; }\n", "-std=gnu17" },
        { "int *p; int x = p;\n", "-std=c17" },
        { "struct s { int a; } v; void f(void) { int x = v; }\n", "-std=c17" },
        { "void f(void) { int *p = 0; long x = p; (void)x; }\n", "-std=c17" },
        { "static int a; long x = &a;\n", "-std=c17" },
        { "static int a; int x = &a;\n", "-std=c17" },
        { "struct s { int a; } v; int x = v;\n", "-std=c17" },
        { "static float f; int *q = &f;\n", "-std=c17" },
        { "int *q = 1.5;\n", "-std=c17" },
        { "void f(void) { static int *p; static int x = p; }\n", "-std=c17" },
        { "int *p; int x = (p);\n", "-std=c17" },
        { "static const char *s = \"x\"; int y = s;\n", "-std=c17" },
        // An absolute address stays a static initializer's constant; an object's address converted to a narrower integer
        // doesn't (clang's second error).
        { "static int a = (int *)5;\n", "-std=gnu17" },
        { "static int g;\nstatic int h = &g;\n", "-std=gnu17" },
        { "static int g[2];\nstatic long h = g;\n", "-std=gnu17" },
        // A typedef'd target and source, a qualified lvalue's target, a function pointer.
        { "typedef int *IP;\nIP f(double d) { return d; }\n", "-std=gnu17" },
        { "typedef int *IP;\nvoid f(IP p) { int x; x = p; }\n", "-std=gnu17" },
        { "void f(int *p) { volatile int v; v = p; }\n", "-std=gnu17" },
        { "void g(int (*)(void));\nvoid h(void);\nvoid f(void) { g(h); }\n", "-std=gnu17" },
        { "void g(int a, int *b);\nvoid f(int i) { g(i, i); }\n", "-std=gnu17" },
        { "void g(int a, char *);\nvoid f(int *p) { g(p, 0); }\n", "-std=gnu17" },
        { "char a[2] = 5;\n", "-std=gnu17" },
        { "long a[2] = 5;\n", "-std=gnu17" },
        { "unsigned short a[2] = 5;\n", "-std=gnu17" },
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/conversion-errors.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const CheckConversionUnit* fixture = units + unit;
        const bool made = parse_test_write(source, fixture->source, strlen(fixture->source));
        char* arguments[] = { (char*)clang, (char*)fixture->standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "conversion errors %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "conversion errors %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, fixture->source,
                     fixture->standard, scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
