// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Operand errors by the driver against clang (#147, #1447), on original fixtures, whole diagnostic output on both paths:
// - an increment's or decrement's operand of no real, pointer or complex type: clang's "cannot increment value of type",
//   the type tested before the operand's modifiability (an array's, a const record's), an _Atomic one's value type
//   printed, a vector's described;
// - a scalar initializer whose conversion failed: that conversion's error alone, where the initializer's walk added a
//   second one ("invalid initializer"); a static object's still gets clang's non-constant one after a pointer or
//   integer conversion clang warns of and makes, where its value is no constant;
// - a binary operator's operands of no type it takes: clang's "invalid operands to binary expression" with both types as
//   written, and two pointers' subtraction of incompatible types (#1872);
// - `__clang__` before `::` in a GNU attribute list: a number, where clang's list ends, unlike a C23 list's.
// Include after c_check_label_attribute_driver_test.c (check_test_diagnostic_lines).

static void check_test_driver_operand_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    typedef struct CheckOperandUnit CheckOperandUnit;
    struct CheckOperandUnit
    {
        const char* source;
        const char* standard;
    };
    static const CheckOperandUnit units[] = {
        // Increments and decrements of vectors, records, arrays and functions.
        { "typedef int v4si __attribute__((vector_size(16)));\nvoid f(v4si a) { a++; }\n", "-std=gnu17" },
        { "typedef float v4sf __attribute__((vector_size(16)));\nvoid f(v4sf b) { --b; }\n", "-std=gnu17" },
        { "typedef int ev4 __attribute__((ext_vector_type(4)));\nvoid f(ev4 a) { ++a; }\n", "-std=gnu17" },
        { "typedef _Bool evb __attribute__((ext_vector_type(8)));\nvoid f(evb b) { b--; }\n", "-std=gnu17" },
        { "typedef int v1si __attribute__((vector_size(4)));\nvoid f(v1si a) { a++; }\n", "-std=gnu17" },
        { "typedef long long v2di __attribute__((vector_size(16)));\nv2di f(v2di a) { return ++a; }\n", "-std=gnu17" },
        { "typedef int v4si __attribute__((vector_size(16)));\ntypedef v4si w4si;\nvoid f(w4si a) { a++; }\n", "-std=gnu17" },
        { "typedef int v4si __attribute__((vector_size(16)));\nvoid f(const v4si a) { a--; }\n", "-std=gnu17" },
        { "typedef int v4si __attribute__((vector_size(16)));\nvoid f(v4si *p) { (*p)++; }\n", "-std=gnu17" },
        { "typedef int v4si __attribute__((vector_size(16)));\nv4si g;\nint f(void) { return sizeof(g++); }\n", "-std=gnu17" },
        { "#define INC(x) x++\ntypedef int v4si __attribute__((vector_size(16)));\nvoid f(v4si a) { INC(a); }\n", "-std=gnu17" },
        { "typedef int v4si __attribute__((vector_size(16)));\nvoid f(v4si a) { a[0]++; ++a[1]; a[2]--; }\n", "-std=gnu17" },
        { "struct s { int a; } v;\nvoid f(void) { v++; }\n", "-std=gnu17" },
        { "struct s { int a; } v;\nvoid f(void) { (v)++; }\n", "-std=gnu17" },
        { "union u { int a; };\nvoid f(union u x) { --x; }\n", "-std=gnu17" },
        { "struct s { int a; };\nvoid f(const struct s c) { c++; }\n", "-std=gnu17" },
        { "struct s { int a; };\nvoid f(_Atomic struct s c) { c++; }\n", "-std=gnu17" },
        { "struct s { int a; };\nvoid f(struct s *p) { p->a++; (*p)++; }\n", "-std=gnu17" },
        { "struct s { int a; };\nstruct s g(void);\nvoid f(void) { g()++; }\n", "-std=gnu17" },
        { "void f(void) { int a[2]; a++; }\n", "-std=gnu17" },
        { "void f(void) { const int a[2] = { 0 }; --a; }\n", "-std=gnu17" },
        { "void g(void);\nvoid f(void) { g++; }\n", "-std=gnu17" },
        { "enum e;\nvoid f(enum e *p) { (*p)++; }\n", "-std=gnu17" },
        // Increments clang takes, and those whose operand's modifiability is the error.
        { "enum e { A };\nvoid f(enum e x, _Bool b, float g, int *p, void *q, _Atomic int a) { x++; b--; ++g; p++; q--; a++; }\n", "-std=gnu17" },
        { "void f(void) { (void)0++; }\n", "-std=gnu17" },
        { "int g(void);\nvoid f(void) { g()++; }\n", "-std=gnu17" },
        // Binary operators' operands of no type they take (#1872): clang's "invalid operands to binary expression" with both
        // types as written, and two pointers' subtraction of incompatible types.
        { "void f(int *q, int n) { (void)(q + 1.0); int (*p)[n] = 0; (void)(p - 2.5f); (void)(1.0 + q); }\n", "-std=gnu17" },
        { "typedef double D;\nvoid f(int *q, D d) { (void)(q + d); q += 1.0; q -= d; }\n", "-std=gnu17" },
        { "struct S { int a; } s;\nvoid f(int *q) { (void)(s + 1); (void)(q * 2); (void)(q % 2); (void)(1.5 % 2); (void)(q + s); }\n", "-std=gnu17" },
        { "void f(int *q) { (void)(q & 1); (void)(q << 1); (void)(1.0 << 1); (void)(q - 1.0i); }\n", "-std=gnu17" },
        { "void g(void);\nvoid f(char c, const int *cq, volatile double vd) { int a[2]; (void)(a * 2); (void)(c % 1.0f); (void)(cq + vd); (void)(g * 2); (void)(a % 1.5); }\n", "-std=gnu17" },
        { "void f(void) { _Bool b = 0; (void)(b ^ 1.0); const double cd = 1; (void)(cd | 1); int x = 0; x <<= 1.0; }\n", "-std=gnu17" },
        { "typedef int I;\nstruct S { int a; };\nvoid f(int *q, float *fp, I *iq, const int *cq, void *vp, struct S *sp, int (*p)[3], int (*r)[4], long *lp)\n{ (void)(q - fp); (void)(iq - fp); (void)(q - cq); (void)(q - vp); (void)(sp - q); (void)(p - r); (void)(q - lp); }\n", "-std=gnu17" },
        // A failed initializer conversion's error alone.
        { "void f(void) { int *p = 0; long x = p; (void)x; }\n", "-std=gnu17" },
        { "void f(void) { int *p = 0; long x = (p); (void)x; }\n", "-std=gnu17" },
        { "void f(void) { float *p = 0; int *q = p; (void)q; }\n", "-std=gnu17" },
        { "void f(void) { const int *p = 0; int x = p; (void)x; }\n", "-std=gnu17" },
        { "void f(void) { char *p = 0; unsigned char x = p; (void)x; }\n", "-std=gnu17" },
        { "void f(void) { int a[2]; int x = a; (void)x; }\n", "-std=gnu17" },
        { "void f(void) { int x = (int *)0; (void)x; }\n", "-std=gnu17" },
        { "void f(void) { float *p = 0; int *q = p; int *r = p; (void)q; (void)r; }\n", "-std=gnu17" },
        { "void f(void) { float *p = 0; int *q = p; int a[2] = { 1, 2, 3 }; (void)q; (void)a; }\n", "-std=gnu17" },
        // A static object's: no constant after the conversion is clang's second error; an address converted to a wide enough
        // integer, or to another pointer, is a constant.
        { "static int a;\nlong x = &a;\n", "-std=gnu17" },
        { "static float f;\nint *q = &f;\n", "-std=gnu17" },
        { "static int a[2];\nlong x = a;\n", "-std=gnu17" },
        { "void f(void) { static float g; static int *q = &g; (void)q; }\n", "-std=gnu17" },
        // An absolute address stays a constant, however narrow the integer it converts to.
        { "static int b = (int *)0;\n", "-std=gnu17" },
        { "static short c = (char *)0x1000;\n", "-std=gnu17" },
        { "static int d = (void *)0;\n", "-std=gnu17" },
        { "static long e = (int *)-1;\n", "-std=gnu17" },
        // __clang__ before :: in a GNU list and a C23 one.
        { "int x __attribute__((__clang__::unused));\n", "-std=gnu17" },
        { "void f(void) __attribute__((__clang__::noreturn));\n", "-std=gnu17" },
        { "int x __attribute__((unused, __clang__::foo));\n", "-std=gnu17" },
        { "int x __attribute__((__clang__));\n", "-std=gnu17" },
        { "#define A __clang__\nint x __attribute__((A::unused));\n", "-std=gnu17" },
        { "[[__clang__::unused]] int x;\n", "-std=c23" },
        { "[[gnu::unused, __clang__::unused]] int x;\n", "-std=c23" },
        { "int x [[__clang__::unused]];\n", "-std=c23" },
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/operand-errors.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const CheckOperandUnit* fixture = units + unit;
        const bool made = parse_test_write(source, fixture->source, strlen(fixture->source));
        char* arguments[] = { (char*)clang, (char*)fixture->standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "operand errors %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "operand errors %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, fixture->source,
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
