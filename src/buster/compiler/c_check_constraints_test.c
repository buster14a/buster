// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The constraint catalogue (#133's first "Done when" item): for each Constraints paragraph of C17's 6.4 to 6.10, a
// violation of each of its rules and a valid twin, and fixtures of the conversions (6.3), the constant expressions (6.6)
// and the initializers (6.7.9). Every source is checked by both paths (check_test_source: the scalar parser and the
// postorder checker, the vector ones) and by clang -fsyntax-only -std=gnu17, and its acceptance and first error's line
// must be clang's and the fixture's own record of them. Each is checked as the corpus check checks a file
// (check_corpus_file: preprocessing, the parse, the checker and the program's control flow, as the driver checks it, by
// both paths alike). Where clang takes a violation, an extension of GNU's or a later standard's or a rule it doesn't
// diagnose, the fixture records that it warns or doesn't (both take it). A directive's (6.10), whose preprocessing errors
// the corpus check gives no place for, runs through the preprocessor's two kernels. A difference from clang the checker
// keeps is marked with the line it rejects at, its reason in docs/c-checker.md ("The constraint catalogue"). Original fixtures,
// written from the standard's text and probed with clang 23; none is imported. Include after c_check_corpus.c, as
// c_check_mutation_test.c is.

typedef enum CheckConstraintOutcome
{
    // clang rejects the violation.
    CHECK_CONSTRAINT_ERROR,
    // clang takes it with a warning it gives by default.
    CHECK_CONSTRAINT_WARNING,
    // clang takes it without one (a warning under -pedantic at most).
    CHECK_CONSTRAINT_ACCEPT,
} CheckConstraintOutcome;

typedef struct CheckConstraintFixture CheckConstraintFixture;
struct CheckConstraintFixture
{
    // The paragraph (C17's), and what its rule requires.
    const char* clause;
    const char* rule;
    CheckConstraintOutcome outcome;
    // The line the checker rejects the violation at where clang takes it, a difference it keeps, or 0.
    uint32_t difference;
    const char* violation;
    const char* valid;
};

static const CheckConstraintFixture check_constraint_fixtures[] =
{
    {
        "6.4p2", "each token has the lexical form of a token", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = 1 ` 2;\n",
        "int a;\n"
        "int b = 1 + 2;\n",
    },
    {
        "6.4p2", "a lone apostrophe is no token", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = ';\n",
        "int a;\n"
        "int b = ';';\n",
    },
    {
        "6.4.3p2", "a universal character name below 00A0 in an identifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int \\u0041b;\n",
        "int a;\n"
        "int \\u00e9b;\n",
    },
    {
        "6.4.3p2", "a universal character name below 00A0 in a string", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "const char *s = \"\\u0041\";\n",
        "int a;\n"
        "const char *s = \"\\u00e9\\u0024\\u0040\\u0060\";\n",
    },
    {
        "6.4.3p2", "a universal character name of a surrogate", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "const char *s = \"\\ud800\";\n",
        "int a;\n"
        "const char *s = \"\\ud7ff\\ue000\";\n",
    },
    {
        "6.4.4p2", "an integer constant no type can represent", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "unsigned long long b = 18446744073709551616;\n",
        "int a;\n"
        "unsigned long long b = 18446744073709551615u;\n",
    },
    {
        "6.4.4p2", "a floating constant too large for its type", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "double b = 1e400;\n",
        "int a;\n"
        "double b = 1e300;\n",
    },
    {
        "6.4.4p2", "a floating constant too small for its type", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "float b = 1e-50f;\n",
        "int a;\n"
        "float b = 1e-30f;\n",
    },
    {
        "6.4.4.4p9", "a hexadecimal escape past unsigned char", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = '\\x100';\n",
        "int a;\n"
        "int b = '\\xff';\n",
    },
    {
        "6.4.4.4p9", "an octal escape past unsigned char", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = '\\777';\n",
        "int a;\n"
        "int b = '\\377';\n",
    },
    {
        "6.4.4.4p9", "a hexadecimal escape past char16_t", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = u'\\x10000';\n",
        "int a;\n"
        "int b = u'\\xffff';\n",
    },
    {
        "6.4.4.4p9", "a hexadecimal escape past char32_t", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = U'\\x100000000';\n",
        "int a;\n"
        "int b = U'\\xffffffff';\n",
    },
    {
        "6.4.5p2", "a wide and a UTF-8 string literal concatenated", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "const void *b = L\"x\" u8\"y\";\n",
        "int a;\n"
        "const void *b = L\"x\" \"y\";\n",
    },
    {
        "6.4.5p2", "a UTF-8 and a wide string literal concatenated", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "const void *b = u8\"x\" L\"y\";\n",
        "int a;\n"
        "const void *b = u8\"x\" \"y\";\n",
    },
    {
        "6.5.1.1p2", "at most one default association", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = _Generic(1, default: 1, default: 2);\n",
        "int a;\n"
        "int b = _Generic(1, default: 1);\n",
    },
    {
        "6.5.1.1p2", "an association's type is complete", CHECK_CONSTRAINT_ACCEPT, 0,
        "struct S;\n"
        "int b = _Generic(1, struct S: 1, default: 2);\n",
        "struct S { int x; };\n"
        "int b = _Generic(1, struct S: 1, default: 2);\n",
    },
    {
        "6.5.1.1p2", "an association's type is an object type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = _Generic(1, int(void): 1, default: 2);\n",
        "int a;\n"
        "int b = _Generic(1, int (*)(void): 1, default: 2);\n",
    },
    {
        "6.5.1.1p2", "an association's type is not variably modified", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    (void)_Generic(1, int[n]: 1, default: 2);\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    (void)n;\n"
        "    (void)_Generic(1, int[2]: 1, default: 2);\n"
        "}\n",
    },
    {
        "6.5.1.1p2", "no two associations of compatible types", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = _Generic(1, int: 1, signed int: 2);\n",
        "int a;\n"
        "int b = _Generic(1, int: 1, unsigned int: 2);\n",
    },
    {
        "6.5.1.1p2", "no two associations of compatible array types", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = _Generic(1, int (*)[]: 1, int (*)[2]: 2, default: 3);\n",
        "int a;\n"
        "int b = _Generic(1, int (*)[3]: 1, int (*)[2]: 2, default: 3);\n",
    },
    {
        "6.5.1.1p2", "without a default, the controlling type matches one association", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b = _Generic(1.0, int: 1, long: 2);\n",
        "int a;\n"
        "int b = _Generic(1.0, int: 1, double: 2);\n",
    },
    {
        "6.5.2.1p1", "the subscripted operand is a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int f(void) { return a[1]; }\n",
        "int a[2];\n"
        "int f(void) { return a[1]; }\n",
    },
    {
        "6.5.2.1p1", "the subscript is an integer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "double d;\n"
        "int f(void) { return p[d]; }\n",
        "int *p;\n"
        "char c;\n"
        "int f(void) { return p[c] + c[p]; }\n",
    },
    {
        "6.5.2.1p1", "the subscripted pointer's pointee is complete", CHECK_CONSTRAINT_ERROR, 0,
        "struct S *p;\n"
        "void f(void) { (void)p[0]; }\n",
        "struct S { int x; } *p;\n"
        "void f(void) { (void)p[0]; }\n",
    },
    {
        "6.5.2.1p1", "the subscripted pointer's pointee is an object type", CHECK_CONSTRAINT_ERROR, 0,
        "void (*p)(void);\n"
        "void f(void) { (void)p[0]; }\n",
        "void (**p)(void);\n"
        "void f(void) { (void)p[0]; }\n",
    },
    {
        "6.5.2.2p1", "the called expression is a pointer to a function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { a(); }\n",
        "int (*a)(void);\n"
        "void f(void) { a(); }\n",
    },
    {
        "6.5.2.2p1", "a called function's result type is complete", CHECK_CONSTRAINT_ERROR, 0,
        "struct S g(void);\n"
        "void f(void) { g(); }\n",
        "struct S { int x; };\n"
        "struct S g(void);\n"
        "void f(void) { g(); }\n",
    },
    {
        "6.5.2.2p2", "as many arguments as a prototype's parameters", CHECK_CONSTRAINT_ERROR, 0,
        "int g(int);\n"
        "int f(void) { return g(1, 2); }\n",
        "int g(int, ...);\n"
        "int f(void) { return g(1, 2); }\n",
    },
    {
        "6.5.2.2p2", "no fewer arguments than a prototype's parameters", CHECK_CONSTRAINT_ERROR, 0,
        "int g(int, int);\n"
        "int f(void) { return g(1); }\n",
        "int g(int, int);\n"
        "int f(void) { return g(1, 2); }\n",
    },
    {
        "6.5.2.2p2", "each argument assignable to its parameter", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; };\n"
        "int g(int);\n"
        "int f(struct S s) { return g(s); }\n",
        "struct S { int x; };\n"
        "int g(int);\n"
        "int f(struct S s) { return g(s.x); }\n",
    },
    {
        "6.5.2.2p2", "an argument of an incompatible pointer type", CHECK_CONSTRAINT_ERROR, 0,
        "int g(int *);\n"
        "int f(long *p) { return g(p); }\n",
        "int g(const int *);\n"
        "int f(int *p) { return g(p); }\n",
    },
    {
        "6.5.2.2p2", "an argument whose pointee differs only in signedness", CHECK_CONSTRAINT_WARNING, 0,
        "int g(char *);\n"
        "int f(unsigned char *p) { return g(p); }\n",
        "int g(char *);\n"
        "int f(char *p) { return g(p); }\n",
    },
    {
        "6.5.2.3p1", "the operand of . is a structure or union", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int f(void) { return a.x; }\n",
        "union { int x; } a;\n"
        "int f(void) { return a.x; }\n",
    },
    {
        "6.5.2.3p1", "the name after . is a member", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "int f(void) { return s.y; }\n",
        "struct S { int x; struct { int y; }; } s;\n"
        "int f(void) { return s.x + s.y; }\n",
    },
    {
        "6.5.2.3p2", "the operand of -> is a pointer to a structure or union", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "int f(void) { return s->x; }\n",
        "struct S { int x; } s;\n"
        "int f(void) { return (&s)->x; }\n",
    },
    {
        "6.5.2.3p2", "the name after -> is a member of the pointee", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } *p;\n"
        "int f(void) { return p->y; }\n",
        "const struct S { int x; } *p;\n"
        "int f(void) { return p->x; }\n",
    },
    {
        "6.5.2.3p2", "the operand of -> points to a structure or union", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "int f(void) { return p->x; }\n",
        "struct S { int x; } **p;\n"
        "int f(void) { return (*p)->x; }\n",
    },
    {
        "6.5.2.4p1", "a postfix increment's operand is real or a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { s++; }\n",
        "int *p;\n"
        "double d;\n"
        "void f(void) { p++; d--; }\n",
    },
    {
        "6.5.2.4p1", "a postfix increment's operand is modifiable", CHECK_CONSTRAINT_ERROR, 0,
        "const int c = 0;\n"
        "void f(void) { c++; }\n",
        "volatile int c = 0;\n"
        "void f(void) { c++; }\n",
    },
    {
        "6.5.2.4p1", "a postfix decrement's operand is an lvalue", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { (x + 1)--; }\n",
        "int a;\n"
        "void f(int x) { x--; }\n",
    },
    {
        "6.5.2.5p1", "a compound literal's type is complete", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "void f(void) { (void)(struct S){ 0 }; }\n",
        "struct S { int x; };\n"
        "void f(void) { (void)(struct S){ 0 }; }\n",
    },
    {
        "6.5.2.5p1", "a compound literal's type is not a variable length array", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    (void)(int[n]){ 0 };\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    (void)(int[]){ n, n };\n"
        "}\n",
    },
    {
        "6.5.2.5p2", "a compound literal's initializer names a member", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { (void)(struct { int x; }){ .y = 1 }; }\n",
        "int a;\n"
        "void f(void) { (void)(struct { int x; }){ .x = 1 }; }\n",
    },
    {
        "6.5.2.5p2", "a compound literal's initializer within its object", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "void f(void) { (void)(int[2]){ 1, 2, 3 }; }\n",
        "int a;\n"
        "void f(void) { (void)(int[2]){ 1, 2 }; }\n",
    },
    {
        "6.5.3.1p1", "a prefix increment's operand is real or a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { ++s; }\n",
        "_Bool b;\n"
        "void f(void) { ++b; }\n",
    },
    {
        "6.5.3.1p1", "a prefix decrement's operand is a modifiable lvalue", CHECK_CONSTRAINT_ERROR, 0,
        "int a[2];\n"
        "void f(void) { --a; }\n",
        "int a[2];\n"
        "void f(void) { --a[0]; }\n",
    },
    {
        "6.5.3.2p1", "the operand of & is an lvalue", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int *f(int x) { return &(x + 1); }\n",
        "int a;\n"
        "int *f(int *x) { return &*x; }\n",
    },
    {
        "6.5.3.2p1", "the operand of & is no bit-field", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int b : 3; } s;\n"
        "void *f(void) { return &s.b; }\n",
        "struct S { int b : 3; int c; } s;\n"
        "void *f(void) { return &s.c; }\n",
    },
    {
        "6.5.3.2p1", "the operand of & is not declared register", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { register int r; int *p = &r; (void)p; }\n",
        "int a;\n"
        "void f(void) { register int r = 1; int x = r; int *p = &x; (void)p; }\n",
    },
    {
        "6.5.3.2p2", "the operand of * is a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int f(void) { return *a; }\n",
        "int *a;\n"
        "int f(void) { return *a; }\n",
    },
    {
        "6.5.3.3p1", "the operand of unary - is arithmetic", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)-p; }\n",
        "_Complex double z;\n"
        "void f(void) { (void)-z; (void)+z; }\n",
    },
    {
        "6.5.3.3p1", "the operand of unary + is arithmetic", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)+s; }\n",
        "struct S { int x; } s;\n"
        "void f(void) { (void)+s.x; }\n",
    },
    {
        "6.5.3.3p1", "the operand of ~ is an integer", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { (void)~d; }\n",
        "long l;\n"
        "void f(void) { (void)~l; }\n",
    },
    {
        "6.5.3.3p1", "the operand of ! is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)!s; }\n",
        "int *p;\n"
        "double d;\n"
        "void f(void) { (void)!p; (void)!d; }\n",
    },
    {
        "6.5.3.4p1", "sizeof of an incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "int a = sizeof(struct S);\n",
        "struct S { int x; };\n"
        "int a = sizeof(struct S);\n",
    },
    {
        "6.5.3.4p1", "sizeof of an expression of incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "extern int e[];\n"
        "int a = sizeof e;\n",
        "extern int e[3];\n"
        "int a = sizeof e;\n",
    },
    {
        "6.5.3.4p1", "sizeof of a function type", CHECK_CONSTRAINT_ACCEPT, 0,
        "int g(void);\n"
        "int a = sizeof g;\n",
        "int g(void);\n"
        "int a = sizeof &g;\n",
    },
    {
        "6.5.3.4p1", "sizeof of void", CHECK_CONSTRAINT_ACCEPT, 0,
        "int b;\n"
        "int a = sizeof(void);\n",
        "int b;\n"
        "int a = sizeof(void *);\n",
    },
    {
        "6.5.3.4p1", "sizeof of a bit-field", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int b : 3; } s;\n"
        "int a = sizeof s.b;\n",
        "struct S { int b : 3; } s;\n"
        "int a = sizeof(s.b + 0);\n",
    },
    {
        "6.5.3.4p1", "_Alignof of an incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "int a = _Alignof(struct S);\n",
        "struct S { int x; };\n"
        "int a = _Alignof(struct S);\n",
    },
    {
        "6.5.3.4p1", "_Alignof of a function type", CHECK_CONSTRAINT_ACCEPT, 0,
        "int b;\n"
        "int a = _Alignof(int(void));\n",
        "int b;\n"
        "int a = _Alignof(int (*)(void));\n",
    },
    {
        "6.5.4p2", "a cast's type is scalar or void", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; };\n"
        "void f(int x) { (void)(struct S)x; }\n",
        "struct S { int x; };\n"
        "void f(int x) { (void)(long)x; }\n",
    },
    {
        "6.5.4p2", "a cast's operand is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(int)s; }\n",
        "struct S { int x; } s;\n"
        "void f(void) { (void)s; }\n",
    },
    {
        "6.5.4p4", "a pointer is not cast to a floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)(double)p; }\n",
        "int *p;\n"
        "void f(void) { (void)(long)p; }\n",
    },
    {
        "6.5.4p4", "a floating value is not cast to a pointer type", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { (void)(int *)d; }\n",
        "long l;\n"
        "void f(void) { (void)(int *)l; }\n",
    },
    {
        "6.5.5p2", "the operands of * are arithmetic", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)(p * 2); }\n",
        "_Complex float z;\n"
        "void f(void) { (void)(z * 2); }\n",
    },
    {
        "6.5.5p2", "the operands of / are arithmetic", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(s / 2); }\n",
        "double d;\n"
        "void f(void) { (void)(d / 2); }\n",
    },
    {
        "6.5.5p2", "the operands of % are integers", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { (void)(d % 2); }\n",
        "unsigned char u;\n"
        "void f(void) { (void)(u % 2); }\n",
    },
    {
        "6.5.6p2", "addition of two pointers", CHECK_CONSTRAINT_ERROR, 0,
        "int *p, *q;\n"
        "void f(void) { (void)(p + q); }\n",
        "int *p;\n"
        "void f(void) { (void)(2 + p); }\n",
    },
    {
        "6.5.6p2", "addition of a pointer to an incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S *p;\n"
        "void f(void) { (void)(p + 1); }\n",
        "struct S { int x; } *p;\n"
        "void f(void) { (void)(p + 1); }\n",
    },
    {
        "6.5.6p2", "addition of a pointer and a floating value", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "double d;\n"
        "void f(void) { (void)(p + d); }\n",
        "int *p;\n"
        "_Bool b;\n"
        "void f(void) { (void)(p + b); }\n",
    },
    {
        "6.5.6p2", "addition of a pointer to void", CHECK_CONSTRAINT_ACCEPT, 0,
        "void *v;\n"
        "void f(void) { (void)(v + 1); }\n",
        "char *v;\n"
        "void f(void) { (void)(v + 1); }\n",
    },
    {
        "6.5.6p3", "subtraction of pointers to incompatible types", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "long *q;\n"
        "long f(void) { return p - q; }\n",
        "const int *p;\n"
        "int *q;\n"
        "long f(void) { return p - q; }\n",
    },
    {
        "6.5.6p3", "subtraction of a pointer from an integer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)(1 - p); }\n",
        "int *p;\n"
        "void f(void) { (void)(p - 1); }\n",
    },
    {
        "6.5.6p3", "subtraction of pointers to an incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S *p, *q;\n"
        "long f(void) { return p - q; }\n",
        "struct S { int x; } *p, *q;\n"
        "long f(void) { return p - q; }\n",
    },
    {
        "6.5.7p2", "a shifted operand is an integer", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { (void)(d << 1); }\n",
        "long l;\n"
        "void f(void) { (void)(l << 1); }\n",
    },
    {
        "6.5.7p2", "a shift count is an integer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)(1 >> p); }\n",
        "char c;\n"
        "void f(void) { (void)(1 >> c); }\n",
    },
    {
        "6.5.8p2", "a relational comparison of structures", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(s < s); }\n",
        "struct S { int x; } s;\n"
        "void f(void) { (void)(s.x < s.x); }\n",
    },
    {
        "6.5.8p2", "a relational comparison of complex values", CHECK_CONSTRAINT_ERROR, 0,
        "_Complex double z;\n"
        "void f(void) { (void)(z < z); }\n",
        "_Complex double z;\n"
        "void f(void) { (void)(__real__ z < __imag__ z); }\n",
    },
    {
        "6.5.8p2", "a relational comparison of a pointer and an integer", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "void f(void) { (void)(p < 1); }\n",
        "int *p;\n"
        "void f(void) { (void)(p < p + 1); }\n",
    },
    {
        "6.5.8p2", "a relational comparison of pointers to incompatible types", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "long *q;\n"
        "void f(void) { (void)(p < q); }\n",
        "int *p;\n"
        "const int *q;\n"
        "void f(void) { (void)(p < q); }\n",
    },
    {
        "6.5.9p2", "an equality comparison of structures", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(s == s); }\n",
        "struct S { int x; } s;\n"
        "void f(void) { (void)(&s == &s); }\n",
    },
    {
        "6.5.9p2", "an equality comparison of pointers to incompatible types", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "long *q;\n"
        "void f(void) { (void)(p == q); }\n",
        "int *p;\n"
        "void *q;\n"
        "void f(void) { (void)(p == q); }\n",
    },
    {
        "6.5.9p2", "an equality comparison of a pointer and a nonzero integer", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "void f(void) { (void)(p == 1); }\n",
        "int *p;\n"
        "void f(void) { (void)(p == 0); (void)(0 != p); }\n",
    },
    {
        "6.5.10p2", "the operands of & are integers", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { (void)(d & 1); }\n",
        "int i;\n"
        "void f(void) { (void)(i & 1u); }\n",
    },
    {
        "6.5.11p2", "the operands of ^ are integers", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { (void)(p ^ 1); }\n",
        "_Bool b;\n"
        "void f(void) { (void)(b ^ 1); }\n",
    },
    {
        "6.5.12p2", "the operands of | are integers", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(1 | s); }\n",
        "char c;\n"
        "void f(void) { (void)(1 | c); }\n",
    },
    {
        "6.5.13p2", "the operands of && are scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(s && 1); }\n",
        "int *p;\n"
        "double d;\n"
        "void f(void) { (void)(p && d); }\n",
    },
    {
        "6.5.14p2", "the operands of || are scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { (void)(1 || s); }\n",
        "int *p;\n"
        "double d;\n"
        "void f(void) { (void)(d || p); }\n",
    },
    {
        "6.5.15p2", "a conditional's first operand is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "int f(void) { return s ? 1 : 2; }\n",
        "int *p;\n"
        "int f(void) { return p ? 1 : 2; }\n",
    },
    {
        "6.5.15p3", "a conditional of a structure and an integer", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(int c) { (void)(c ? s : 1); }\n",
        "struct S { int x; } s;\n"
        "void f(int c) { (void)(c ? s : s); }\n",
    },
    {
        "6.5.15p3", "a conditional of two structure types", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "struct T { int x; } t;\n"
        "void f(int c) { (void)(c ? s : t); }\n",
        "struct S { int x; } s;\n"
        "const struct S t;\n"
        "void f(int c) { (void)(c ? s : t); }\n",
    },
    {
        "6.5.15p3", "a conditional of void and a value", CHECK_CONSTRAINT_ACCEPT, 0,
        "void g(void);\n"
        "void f(int c) { c ? g() : 1; }\n",
        "void g(void);\n"
        "void f(int c) { c ? g() : (void)1; }\n",
    },
    {
        "6.5.15p3", "a conditional of pointers to incompatible types", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "long *q;\n"
        "void f(int c) { (void)(c ? p : q); }\n",
        "int *p;\n"
        "void *q;\n"
        "void f(int c) { (void)(c ? p : q); }\n",
    },
    {
        "6.5.15p3", "a conditional of a pointer and a nonzero integer", CHECK_CONSTRAINT_WARNING, 0,
        "int *p;\n"
        "void f(int c) { (void)(c ? p : 1); }\n",
        "int *p;\n"
        "void f(int c) { (void)(c ? p : 0); }\n",
    },
    {
        "6.5.16p2", "an assignment's left operand is not const", CHECK_CONSTRAINT_ERROR, 0,
        "const int c;\n"
        "void f(void) { c = 1; }\n",
        "int c;\n"
        "void f(void) { c = 1; }\n",
    },
    {
        "6.5.16p2", "an assignment's left operand is not an array", CHECK_CONSTRAINT_ERROR, 0,
        "int a[2], b[2];\n"
        "void f(void) { a = b; }\n",
        "int *a, b[2];\n"
        "void f(void) { a = b; }\n",
    },
    {
        "6.5.16p2", "an assignment's left operand is an lvalue", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { x + 1 = 2; }\n",
        "int a;\n"
        "void f(int x) { *(&x + 0) = 2; }\n",
    },
    {
        "6.5.16p2", "an assignment's left structure has no const member", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { const int a; } s, t;\n"
        "void f(void) { s = t; }\n",
        "struct S { int a; const int *b; } s, t;\n"
        "void f(void) { s = t; }\n",
    },
    {
        "6.5.16p2", "a compound assignment's left operand is modifiable", CHECK_CONSTRAINT_ERROR, 0,
        "const int c;\n"
        "void f(void) { c += 1; }\n",
        "int c;\n"
        "void f(void) { c += 1; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of incompatible structures", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "struct T { int x; } t;\n"
        "void f(void) { s = t; }\n",
        "struct S { int x; } s;\n"
        "const struct S t;\n"
        "void f(void) { s = t; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of a structure to an integer", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "int i;\n"
        "void f(void) { i = s; }\n",
        "struct S { int x; } s;\n"
        "int i;\n"
        "void f(void) { i = s.x; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of incompatible pointers", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "long *q;\n"
        "void f(void) { p = q; }\n",
        "const int *p;\n"
        "int *q;\n"
        "void f(void) { p = q; }\n",
    },
    {
        "6.5.16.1p1", "an assignment that discards qualifiers", CHECK_CONSTRAINT_WARNING, 0,
        "const int *c;\n"
        "int *p;\n"
        "void f(void) { p = c; }\n",
        "const int *c;\n"
        "const volatile int *p;\n"
        "void f(void) { p = c; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of an integer to a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { p = 1; }\n",
        "int *p;\n"
        "void f(void) { p = 0; p = (void *)0; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of a pointer to an integer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "int i;\n"
        "void f(void) { i = p; }\n",
        "int *p;\n"
        "_Bool b;\n"
        "void f(void) { b = p; }\n",
    },
    {
        "6.5.16.1p1", "an assignment of a pointer to void and a pointer to an object", CHECK_CONSTRAINT_WARNING, 0,
        "int **p;\n"
        "const void *v;\n"
        "void f(void) { p = v; }\n",
        "int **p;\n"
        "void *v;\n"
        "void f(void) { p = v; v = p; }\n",
    },
    {
        "6.5.16.2p1", "+= of a pointer and a floating value", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "double d;\n"
        "void f(void) { p += d; }\n",
        "int *p;\n"
        "long l;\n"
        "void f(void) { p += l; p -= l; }\n",
    },
    {
        "6.5.16.2p1", "+= of a pointer to an incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S *p;\n"
        "void f(void) { p += 1; }\n",
        "struct S { int x; } *p;\n"
        "void f(void) { p += 1; }\n",
    },
    {
        "6.5.16.2p1", "-= of two pointers", CHECK_CONSTRAINT_ERROR, 0,
        "int *p, *q;\n"
        "void f(void) { p -= q; }\n",
        "int *p, *q;\n"
        "long d;\n"
        "void f(void) { d = p - q; }\n",
    },
    {
        "6.5.16.2p2", "*= of a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int *p;\n"
        "void f(void) { p *= 2; }\n",
        "double d;\n"
        "void f(void) { d *= 2; }\n",
    },
    {
        "6.5.16.2p2", "%= of a floating value", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { d %= 2; }\n",
        "int i;\n"
        "void f(void) { i %= 2; }\n",
    },
    {
        "6.5.16.2p2", "<<= of a floating value", CHECK_CONSTRAINT_ERROR, 0,
        "double d;\n"
        "void f(void) { d <<= 1; }\n",
        "unsigned u;\n"
        "void f(void) { u <<= 1; u >>= 1; u &= 1; u ^= 1; u |= 1; }\n",
    },
    {
        "6.6p3", "a constant expression contains no assignment", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "enum { E = (x = 1) };\n",
        "int x;\n"
        "enum { E = sizeof(x = 1) };\n",
    },
    {
        "6.6p3", "a constant expression contains no function call", CHECK_CONSTRAINT_ERROR, 0,
        "int g(void);\n"
        "_Static_assert(g(), \"\");\n",
        "int g(void);\n"
        "_Static_assert(sizeof(g()) == sizeof(int), \"\");\n",
    },
    {
        "6.6p3", "a constant expression contains no increment", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "enum { E = x++ };\n",
        "int x;\n"
        "enum { E = sizeof(x++) };\n",
    },
    {
        "6.6p3", "an integer constant expression contains no comma operator", CHECK_CONSTRAINT_WARNING, 0,
        "int x;\n"
        "int a[(1, 2)];\n",
        "int x;\n"
        "int a[(1 + 2)];\n",
    },
    {
        "6.6p4", "a constant expression's value in range of its type", CHECK_CONSTRAINT_WARNING, 0,
        "int x;\n"
        "_Static_assert(0x7fffffff + 1 < 0, \"\");\n",
        "int x;\n"
        "_Static_assert(0x7fffffff + 1u > 0, \"\");\n",
    },
    {
        "6.6p4", "an enumerator's constant expression in range", CHECK_CONSTRAINT_WARNING, 0,
        "int x;\n"
        "enum { E = 0x7fffffff + 1 };\n",
        "int x;\n"
        "enum { E = 0x7fffffff };\n",
    },
    {
        "6.6p6", "an integer constant expression of a variable", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "int a[x];\n",
        "enum { X = 3 };\n"
        "int a[X];\n",
    },
    {
        "6.6p6", "an integer constant expression of a const object", CHECK_CONSTRAINT_ACCEPT, 0,
        "const int k = 3;\n"
        "enum { E = k };\n",
        "const int k = 3;\n"
        "enum { E = sizeof k };\n",
    },
    {
        "6.6p6", "a floating operand that is not a cast's", CHECK_CONSTRAINT_ACCEPT, 0,
        "int x;\n"
        "enum { E = (int)(1.0 + 2.0) };\n",
        "int x;\n"
        "enum { E = (int)1.5 + (int)2.5 };\n",
    },
    {
        "6.6p7", "a static initializer of a variable's value", CHECK_CONSTRAINT_ERROR, 0,
        "int y;\n"
        "int x = y;\n",
        "int y;\n"
        "int *x = &y + 1;\n",
    },
    {
        "6.6p7", "a static initializer of a call", CHECK_CONSTRAINT_ERROR, 0,
        "int g(void);\n"
        "int x = g();\n",
        "int g(void);\n"
        "int (*x)(void) = g;\n",
    },
    {
        "6.6p9", "a block-scope static's address constant of an automatic object", CHECK_CONSTRAINT_ERROR, 0,
        "int g;\n"
        "void f(void) { int x; static int *p = &x; (void)p; }\n",
        "int g;\n"
        "void f(void) { static int x; static int *p = &x; (void)p; }\n",
    },
    {
        "6.6p9", "an address constant of a cast to a narrower integer", CHECK_CONSTRAINT_ERROR, 0,
        "static int x;\n"
        "int i = (int)&x;\n",
        "static int x;\n"
        "static int a[3];\n"
        "int *i = a + 2;\n",
    },
    {
        "6.7p2", "a declaration declares something", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int;\n",
        "int a;\n"
        "struct S { int x; };\n",
    },
    {
        "6.7p2", "an untagged structure that declares nothing", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "struct { int x; };\n",
        "int a;\n"
        "struct { int x; } b;\n",
    },
    {
        "6.7p3", "one declaration of an identifier without linkage", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { int x; int x; }\n",
        "int a;\n"
        "void f(void) { int x; { int x; (void)x; } (void)x; }\n",
    },
    {
        "6.7p3", "a parameter is redeclared in the function's block", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { int x; }\n",
        "int a;\n"
        "void f(int x) { { int x = 1; (void)x; } (void)x; }\n",
    },
    {
        "6.7p3", "a typedef redefined to another type", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int T;\n"
        "typedef long T;\n",
        "typedef int T;\n"
        "typedef int T;\n",
    },
    {
        "6.7p3", "a typedef of a variably modified type redefined", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    typedef int A[n];\n"
        "    typedef int A[n];\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    typedef int A[n];\n"
        "    A a;\n"
        "    (void)a;\n"
        "}\n",
    },
    {
        "6.7p4", "declarations of an object with incompatible types", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "long a;\n",
        "int a[];\n"
        "int a[3];\n",
    },
    {
        "6.7p4", "declarations of a function with incompatible types", CHECK_CONSTRAINT_ERROR, 0,
        "int g(int);\n"
        "int g(long);\n",
        "int g();\n"
        "int g(int x) { return x; }\n",
    },
    {
        "6.7p4", "a block-scope extern of an incompatible type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { extern long a; }\n",
        "int a;\n"
        "void f(void) { extern int a; (void)a; }\n",
    },
    {
        "6.7.1p2", "at most one storage-class specifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "static extern int b;\n",
        "int a;\n"
        "static _Thread_local int b;\n",
    },
    {
        "6.7.1p2", "typedef with another storage-class specifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "typedef static int T;\n",
        "int a;\n"
        "typedef int T;\n",
    },
    {
        "6.7.1p3", "a block-scope _Thread_local object is static or extern", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { _Thread_local int t; }\n",
        "int a;\n"
        "void f(void) { static _Thread_local int t; extern _Thread_local int u; (void)t; (void)u; }\n",
    },
    {
        "6.7.1p3", "_Thread_local in every declaration of the object", CHECK_CONSTRAINT_ERROR, 0,
        "extern _Thread_local int t;\n"
        "extern int t;\n",
        "extern _Thread_local int t;\n"
        "_Thread_local int t;\n",
    },
    {
        "6.7.1p4", "_Thread_local on a function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Thread_local void g(void);\n",
        "int a;\n"
        "void g(void);\n",
    },
    {
        "6.7.2p2", "a declaration has a type specifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "static b;\n",
        "int a;\n"
        "static int b;\n",
    },
    {
        "6.7.2p2", "a member declaration has a type specifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { const x; };\n",
        "int a;\n"
        "struct S { const int x; };\n",
    },
    {
        "6.7.2p2", "long char is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "long char c;\n",
        "int a;\n"
        "signed char c;\n",
    },
    {
        "6.7.2p2", "unsigned float is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "unsigned float f;\n",
        "int a;\n"
        "long double f;\n",
    },
    {
        "6.7.2p2", "short long is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "short long x;\n",
        "int a;\n"
        "long long unsigned int x;\n",
    },
    {
        "6.7.2p2", "long long long is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "long long long x;\n",
        "int a;\n"
        "long int long x;\n",
    },
    {
        "6.7.2p2", "int double is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int double y;\n",
        "int a;\n"
        "double _Complex y;\n",
    },
    {
        "6.7.2p2", "_Bool with a sign is no type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "unsigned _Bool b;\n",
        "int a;\n"
        "_Bool b;\n",
    },
    {
        "6.7.2p2", "plain _Complex has no type", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "_Complex c;\n",
        "int a;\n"
        "_Complex double c;\n",
    },
    {
        "6.7.2.1p2", "a member declaration has a declarator", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "struct S { int; int x; };\n",
        "int a;\n"
        "struct S { struct { int y; }; int x; };\n",
    },
    {
        "6.7.2.1p3", "a member of incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct T;\n"
        "struct S { struct T t; };\n",
        "struct T;\n"
        "struct S { struct T *t; };\n",
    },
    {
        "6.7.2.1p3", "a structure that contains itself", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { struct S s; };\n",
        "int a;\n"
        "struct S { struct S *next; };\n",
    },
    {
        "6.7.2.1p3", "a member of function type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int g(void); };\n",
        "int a;\n"
        "struct S { int (*g)(void); };\n",
    },
    {
        "6.7.2.1p3", "a member of void type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { void v; };\n",
        "int a;\n"
        "struct S { void *v; };\n",
    },
    {
        "6.7.2.1p3", "a flexible array member is last", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int n; int x[]; int y; };\n",
        "int a;\n"
        "struct S { int n; int y; int x[]; };\n",
    },
    {
        "6.7.2.1p3", "a flexible array member in an otherwise empty structure", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "struct S { int x[]; };\n",
        "int a;\n"
        "struct S { int n; int x[]; };\n",
    },
    {
        "6.7.2.1p3", "a flexible array member in a union", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "union U { int n; int x[]; };\n",
        "int a;\n"
        "union U { int n; int x[1]; };\n",
    },
    {
        "6.7.2.1p3", "a structure with a flexible array member as a member", CHECK_CONSTRAINT_WARNING, 0,
        "struct S { int n; int x[]; };\n"
        "struct T { struct S s; int y; };\n",
        "struct S { int n; int x[]; };\n"
        "struct T { struct S *s; int y; };\n",
    },
    {
        "6.7.2.1p3", "a structure with a flexible array member as an element", CHECK_CONSTRAINT_ACCEPT, 0,
        "struct S { int n; int x[]; };\n"
        "struct S a[2];\n",
        "struct S { int n; int x[]; };\n"
        "struct S *a[2];\n",
    },
    {
        "6.7.2.1p4", "a bit-field width is nonnegative", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int b : -1; };\n",
        "int a;\n"
        "struct S { int b : 1; };\n",
    },
    {
        "6.7.2.1p4", "a bit-field width within its type's", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int b : 33; };\n",
        "int a;\n"
        "struct S { unsigned b : 32; long c : 64; };\n",
    },
    {
        "6.7.2.1p4", "a _Bool bit-field width of one", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { _Bool b : 2; };\n",
        "int a;\n"
        "struct S { _Bool b : 1; };\n",
    },
    {
        "6.7.2.1p4", "a bit-field width is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int w;\n"
        "struct S { int b : w; };\n",
        "enum { W = 3 };\n"
        "struct S { int b : W; };\n",
    },
    {
        "6.7.2.1p4", "a bit-field width of integer type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int b : 1.0; };\n",
        "int a;\n"
        "struct S { int b : (int)1.0; };\n",
    },
    {
        "6.7.2.1p4", "a zero-width bit-field is unnamed", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int x; int b : 0; };\n",
        "int a;\n"
        "struct S { int x; int : 0; int y; };\n",
    },
    {
        "6.7.2.1p5", "a bit-field of floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { double b : 3; };\n",
        "int a;\n"
        "struct S { char b : 3; long c : 40; };\n",
    },
    {
        "6.7.2.1p5", "a bit-field of pointer type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int *p : 3; };\n",
        "int a;\n"
        "struct S { enum { X, Y } e : 2; };\n",
    },
    {
        "6.7.2.2p2", "an enumerator's value is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "enum E { A = x };\n",
        "int x;\n"
        "enum E { A = 1, B = A + 1 };\n",
    },
    {
        "6.7.2.2p2", "an enumerator's value of floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "enum E { A = 1.0 };\n",
        "int x;\n"
        "enum E { A = (int)1.0 };\n",
    },
    {
        "6.7.2.2p2", "an enumerator's value representable as an int", CHECK_CONSTRAINT_ACCEPT, 0,
        "int x;\n"
        "enum E { A = 0x80000000 };\n",
        "int x;\n"
        "enum E { A = -0x7fffffff - 1, B = 0x7fffffff };\n",
    },
    {
        "6.7.2.3p1", "a structure's content defined once", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; };\n"
        "struct S { int x; };\n",
        "struct S { int x; };\n"
        "void f(void) { struct S { long y; } s; (void)s; }\n",
    },
    {
        "6.7.2.3p1", "an enumeration's content defined once", CHECK_CONSTRAINT_ERROR, 0,
        "enum E { A };\n"
        "enum E { B };\n",
        "enum E { A };\n"
        "enum E e = A;\n",
    },
    {
        "6.7.2.3p2", "a tag with the same choice of struct, union or enum", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "union S *p;\n",
        "struct S;\n"
        "struct S *p;\n",
    },
    {
        "6.7.2.3p2", "an enumeration tag used as a structure", CHECK_CONSTRAINT_ERROR, 0,
        "enum E { A };\n"
        "struct E *p;\n",
        "enum E { A };\n"
        "enum E *p;\n",
    },
    {
        "6.7.2.3p3", "an enumeration named before it is complete", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "enum E *p;\n",
        "enum E { A };\n"
        "enum E *p;\n",
    },
    {
        "6.7.2.4p3", "an atomic type specifier of an array type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Atomic(int[2]) b;\n",
        "int a;\n"
        "_Atomic(int *) b;\n",
    },
    {
        "6.7.2.4p3", "an atomic type specifier of a function type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Atomic(int(void)) b;\n",
        "int a;\n"
        "_Atomic(int (*)(void)) b;\n",
    },
    {
        "6.7.2.4p3", "an atomic type specifier of an atomic type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Atomic(_Atomic int) b;\n",
        "int a;\n"
        "_Atomic(long) b;\n",
    },
    {
        "6.7.2.4p3", "an atomic type specifier of a qualified type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Atomic(const int) b;\n",
        "int a;\n"
        "const _Atomic(int) b;\n",
    },
    {
        "6.7.3p2", "restrict on a non-pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "restrict int b;\n",
        "int a;\n"
        "int *restrict b;\n",
    },
    {
        "6.7.3p2", "restrict on a pointer to a function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void (*restrict b)(void);\n",
        "int a;\n"
        "void (**restrict b)(void);\n",
    },
    {
        "6.7.3p3", "_Atomic on an array type", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int A[2];\n"
        "_Atomic A b;\n",
        "typedef int *A;\n"
        "_Atomic A b;\n",
    },
    {
        "6.7.3p3", "_Atomic on a function type", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int F(void);\n"
        "_Atomic F b;\n",
        "typedef int F(void);\n"
        "_Atomic(F *) b;\n",
    },
    {
        "6.7.4p2", "inline on an object", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "inline int b;\n",
        "int a;\n"
        "inline int b(void) { return 1; }\n",
    },
    {
        "6.7.4p2", "_Noreturn on an object", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Noreturn int b;\n",
        "int a;\n"
        "_Noreturn void b(void);\n",
    },
    {
        "6.7.4p3", "an external inline definition defines a modifiable static object", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "inline int f(void) { static int x; return x; }\n",
        "int a;\n"
        "inline int f(void) { static const int x = 1; return x; }\n",
    },
    {
        "6.7.4p3", "an external inline definition refers to an internal identifier", CHECK_CONSTRAINT_ACCEPT, 0,
        "static int s;\n"
        "inline int f(void) { return s; }\n",
        "static int s;\n"
        "static inline int f(void) { return s; }\n",
    },
    {
        "6.7.4p4", "main declared inline", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "inline int main(void) { return 0; }\n",
        "int a;\n"
        "int main(void) { return 0; }\n",
    },
    {
        "6.7.5p2", "an alignment specifier on a typedef", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "typedef _Alignas(8) int T;\n",
        "int a;\n"
        "typedef int T;\n"
        "_Alignas(8) T b;\n",
    },
    {
        "6.7.5p2", "an alignment specifier on a bit-field", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { _Alignas(8) int b : 3; };\n",
        "int a;\n"
        "struct S { _Alignas(8) int b; };\n",
    },
    {
        "6.7.5p2", "an alignment specifier on a function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Alignas(8) void g(void);\n",
        "int a;\n"
        "_Alignas(8) char g;\n",
    },
    {
        "6.7.5p2", "an alignment specifier on a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(_Alignas(8) int p);\n",
        "int a;\n"
        "void g(int p);\n",
    },
    {
        "6.7.5p2", "an alignment specifier on a register object", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(void) { register _Alignas(8) int r; }\n",
        "int a;\n"
        "void g(void) { _Alignas(8) int r; (void)r; }\n",
    },
    {
        "6.7.5p3", "an alignment is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int n;\n"
        "_Alignas(n) int b;\n",
        "enum { N = 8 };\n"
        "_Alignas(N) int b;\n",
    },
    {
        "6.7.5p3", "an alignment is a power of two", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Alignas(3) int b;\n",
        "int a;\n"
        "_Alignas(0) int b;\n"
        "_Alignas(64) char c[64];\n",
    },
    {
        "6.7.5p4", "an alignment no less strict than the type's", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Alignas(1) int b;\n",
        "int a;\n"
        "_Alignas(16) _Alignas(4) int b;\n",
    },
    {
        "6.7.6.2p1", "an array size of integer type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[1.5];\n",
        "int a;\n"
        "int b[(int)1.5 + 1];\n",
    },
    {
        "6.7.6.2p1", "an array size greater than zero", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[-1];\n",
        "int a;\n"
        "int b[1];\n",
    },
    {
        "6.7.6.2p1", "an array size of zero", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "int b[0];\n",
        "int a;\n"
        "int b[1];\n",
    },
    {
        "6.7.6.2p1", "an array of incomplete elements", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "struct S b[2];\n",
        "struct S { int x; };\n"
        "struct S b[2];\n",
    },
    {
        "6.7.6.2p1", "an array of functions", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int F(void);\n"
        "F b[2];\n",
        "typedef int F(void);\n"
        "F *b[2];\n",
    },
    {
        "6.7.6.2p1", "an array of void", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void b[2];\n",
        "int a;\n"
        "void *b[2];\n",
    },
    {
        "6.7.6.2p1", "static in an array declarator outside a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[static 2];\n",
        "int a;\n"
        "void g(int b[static 2]);\n",
    },
    {
        "6.7.6.2p1", "a qualifier in an array declarator outside a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[const 2];\n",
        "int a;\n"
        "void g(int b[const 2]);\n",
    },
    {
        "6.7.6.2p1", "static in an inner array declarator", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(int b[2][static 2]);\n",
        "int a;\n"
        "void g(int b[static 2][2]);\n",
    },
    {
        "6.7.6.2p2", "a variable length array at file scope", CHECK_CONSTRAINT_ERROR, 0,
        "int n = 3;\n"
        "int b[n];\n",
        "int n = 3;\n"
        "void f(void) { int b[n]; (void)b; }\n",
    },
    {
        "6.7.6.2p2", "a variable length array member", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    struct S { int b[n]; };\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    struct S { int *b; };\n"
        "    (void)n;\n"
        "}\n",
    },
    {
        "6.7.6.2p2", "an extern variable length array", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    extern int b[n];\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    extern int b[];\n"
        "    (void)n;\n"
        "}\n",
    },
    {
        "6.7.6.2p2", "a static variable length array", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    static int b[n];\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    static int b[3];\n"
        "    (void)b;\n"
        "    (void)n;\n"
        "}\n",
    },
    {
        "6.7.6.3p1", "a function returning an array", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int A[2];\n"
        "A g(void);\n",
        "typedef int A[2];\n"
        "A *g(void);\n",
    },
    {
        "6.7.6.3p1", "a function returning a function", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int F(void);\n"
        "F g(void);\n",
        "typedef int F(void);\n"
        "F *g(void);\n",
    },
    {
        "6.7.6.3p2", "static on a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(static int x);\n",
        "int a;\n"
        "void g(register int x);\n",
    },
    {
        "6.7.6.3p2", "extern on a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(extern int x);\n",
        "int a;\n"
        "void g(const int x);\n",
    },
    {
        "6.7.6.3p3", "an identifier list in a declaration", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(x, y);\n",
        "int a;\n"
        "int g();\n"
        "int h(x, y) int x, y; { return x + y; }\n",
    },
    {
        "6.7.6.3p4", "a definition's parameter of incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "void g(struct S s) { }\n",
        "struct S;\n"
        "void g(struct S s);\n",
    },
    {
        "6.7.6.3p4", "a definition's parameter of void type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void g(void x) { }\n",
        "int a;\n"
        "void g(int x[]) { (void)x; }\n",
    },
    {
        "6.7.8p2", "a typedef of a variably modified type at file scope", CHECK_CONSTRAINT_ERROR, 0,
        "int n = 3;\n"
        "typedef int A[n];\n",
        "int n = 3;\n"
        "void f(void) { typedef int A[n]; A a; (void)a; }\n",
    },
    {
        "6.7.9p2", "an array initializer within its array", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int b[2] = { 1, 2, 3 };\n",
        "int a;\n"
        "int b[2] = { 1, 2 };\n",
    },
    {
        "6.7.9p2", "a structure initializer within its structure", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "struct S { int x; } b = { 1, 2 };\n",
        "int a;\n"
        "struct S { int x; } b = { 1 };\n",
    },
    {
        "6.7.9p2", "a scalar initializer of one expression", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int b = { 1, 2 };\n",
        "int a;\n"
        "int b = { 1 };\n",
    },
    {
        "6.7.9p2", "a designator within its array", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[2] = { [2] = 1 };\n",
        "int a;\n"
        "int b[2] = { [1] = 1 };\n",
    },
    {
        "6.7.9p3", "an initialized object of incomplete type", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "struct S b = { 0 };\n",
        "struct S { int x; };\n"
        "struct S b = { 0 };\n",
    },
    {
        "6.7.9p3", "an initialized variable length array", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    int b[n] = { 0 };\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    int b[n];\n"
        "    (void)b;\n"
        "}\n",
    },
    {
        "6.7.9p3", "an initialized function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(void) = 0;\n",
        "int a;\n"
        "int (*g)(void) = 0;\n",
    },
    {
        "6.7.9p4", "a static object's initializer is constant", CHECK_CONSTRAINT_ERROR, 0,
        "int g(void);\n"
        "void f(int y) { static int x = y; (void)x; }\n",
        "int g(void);\n"
        "void f(int y) { static int x = sizeof y; (void)x; }\n",
    },
    {
        "6.7.9p4", "a static aggregate's initializer is constant", CHECK_CONSTRAINT_ERROR, 0,
        "int y;\n"
        "int b[2] = { 1, y };\n",
        "int y;\n"
        "int *b[2] = { 0, &y };\n",
    },
    {
        "6.7.9p5", "a block-scope extern without an initializer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { extern int x = 1; }\n",
        "int a;\n"
        "void f(void) { extern int x; static int y = 1; (void)x; (void)y; }\n",
    },
    {
        "6.7.9p6", "an array designator for a structure", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int x; } b = { [0] = 1 };\n",
        "int a;\n"
        "struct S { int x[2]; } b = { .x[0] = 1 };\n",
    },
    {
        "6.7.9p6", "an array designator is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int n;\n"
        "int b[2] = { [n] = 1 };\n",
        "enum { N = 1 };\n"
        "int b[2] = { [N] = 1 };\n",
    },
    {
        "6.7.9p6", "an array designator is nonnegative", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[] = { [-1] = 1 };\n",
        "int a;\n"
        "int b[] = { [5] = 1 };\n",
    },
    {
        "6.7.9p7", "a member designator for an array", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int b[2] = { .x = 1 };\n",
        "int a;\n"
        "struct { int x; } b[2] = { [1].x = 1 };\n",
    },
    {
        "6.7.9p7", "a member designator names a member", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "struct S { int x; } b = { .y = 1 };\n",
        "int a;\n"
        "struct S { int x, y; } b = { .y = 2, .x = 1 };\n",
    },
    {
        "6.7.9p14", "a character array from a wide string", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "char b[] = L\"x\";\n",
        "int a;\n"
        "int b[] = L\"x\";\n",
    },
    {
        "6.7.9p14", "a character array's string too long", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "char b[2] = \"abc\";\n",
        "int a;\n"
        "char b[3] = \"abc\";\n",
    },
    {
        "6.7.9p11", "a scalar's braces around braces", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int b = { { 1 } };\n",
        "int a;\n"
        "int b = { 1 };\n",
    },
    {
        "6.7.9p19", "a designator overrides an earlier initializer", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int b[2] = { [0] = 1, [0] = 2 };\n",
        "int a;\n"
        "int b[2] = { [0] = 1, [1] = 2 };\n",
    },
    {
        "6.7.9p17", "nested designators and brace elision", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "struct S { int x[2]; struct { int y; } in; } b = { .in.y = 1, .x[1] = 2, 3 };\n",
        "int a;\n"
        "int b[2][2] = { [1][0] = 1, 2, 3, 4 };\n",
    },
    {
        "6.7.10p2", "a static assertion's expression is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "_Static_assert(x, \"\");\n",
        "enum { X = 1 };\n"
        "_Static_assert(X, \"\");\n",
    },
    {
        "6.7.10p2", "a static assertion of floating type", CHECK_CONSTRAINT_ACCEPT, 0,
        "int x;\n"
        "_Static_assert(1.0, \"\");\n",
        "int x;\n"
        "_Static_assert((int)1.0, \"\");\n",
    },
    {
        "6.7.10p3", "a static assertion that fails", CHECK_CONSTRAINT_ERROR, 0,
        "int x;\n"
        "_Static_assert(sizeof(int) == 2, \"int\");\n",
        "int x;\n"
        "_Static_assert(sizeof(int) == 4, \"int\");\n",
    },
    {
        "6.8.1p2", "a case label inside a switch", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { case 1: ; }\n",
        "int a;\n"
        "void f(int x) { switch (x) { case 1: ; } }\n",
    },
    {
        "6.8.1p2", "a default label inside a switch", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { default: ; }\n",
        "int a;\n"
        "void f(int x) { switch (x) { default: ; } }\n",
    },
    {
        "6.8.1p3", "label names are unique within a function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { l: ; l: ; }\n",
        "int a;\n"
        "void f(void) { l: ; }\n"
        "void g(void) { l: ; }\n",
    },
    {
        "6.8.4.1p1", "an if's controlling expression is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { if (s) { } }\n",
        "int *p;\n"
        "void f(void) { if (p) { } }\n",
    },
    {
        "6.8.4.2p1", "a switch's controlling expression is an integer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(double d) { switch (d) { } }\n",
        "int a;\n"
        "void f(char c) { switch (c) { } }\n",
    },
    {
        "6.8.4.2p1", "a switch's controlling expression is not a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int *p) { switch (p) { } }\n",
        "int a;\n"
        "void f(int *p) { switch (*p) { } }\n",
    },
    {
        "6.8.4.2p2", "a case label within a variably modified identifier's scope", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    switch (n) { int b[n]; case 1: (void)b; }\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    int b[n];\n"
        "    switch (n) { case 1: (void)b; }\n"
        "}\n",
    },
    {
        "6.8.4.2p3", "a case label is an integer constant expression", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x, int y) { switch (x) { case y: ; } }\n",
        "enum { Y = 2 };\n"
        "void f(int x) { switch (x) { case Y: ; } }\n",
    },
    {
        "6.8.4.2p3", "case values differ", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { switch (x) { case 1: case 1: ; } }\n",
        "int a;\n"
        "void f(int x) { switch (x) { case 1: case 2: ; } }\n",
    },
    {
        "6.8.4.2p3", "case values differ after their conversion", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(unsigned x) { switch (x) { case -1: case 0xffffffff: ; } }\n",
        "int a;\n"
        "void f(long x) { switch (x) { case -1: case 0xffffffff: ; } }\n",
    },
    {
        "6.8.4.2p3", "a case label of floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { switch (x) { case 1.5: ; } }\n",
        "int a;\n"
        "void f(int x) { switch (x) { case (int)1.5: ; } }\n",
    },
    {
        "6.8.4.2p3", "at most one default label in a switch", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { switch (x) { default: ; default: ; } }\n",
        "int a;\n"
        "void f(int x) { switch (x) { default: switch (x) { default: ; } } }\n",
    },
    {
        "6.8.5p2", "a while's controlling expression is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { while (s) { } }\n",
        "double d;\n"
        "void f(void) { while (d) { } }\n",
    },
    {
        "6.8.5p2", "a do's controlling expression is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { do { } while (s); }\n",
        "int *p;\n"
        "void f(void) { do { } while (p); }\n",
    },
    {
        "6.8.5p2", "a for's controlling expression is scalar", CHECK_CONSTRAINT_ERROR, 0,
        "struct S { int x; } s;\n"
        "void f(void) { for (; s; ) { } }\n",
        "long l;\n"
        "void f(void) { for (; l; ) { } }\n",
    },
    {
        "6.8.5p3", "a for's declaration of a static object", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "void f(void) { for (static int i = 0; i < 1; ) { } }\n",
        "int a;\n"
        "void f(void) { for (register int i = 0; i < 1; i++) { } }\n",
    },
    {
        "6.8.5p3", "a for's declaration of a typedef", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "void f(void) { for (typedef int T; ; ) { } }\n",
        "int a;\n"
        "void f(void) { for (auto int i = 0; i < 1; i++) { } }\n",
    },
    {
        "6.8.5p3", "a for's declaration of an extern object", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "void f(void) { for (extern int i; ; ) { } }\n",
        "int a;\n"
        "void f(void) { for (int i = 0, j = 1; i < j; i++) { } }\n",
    },
    {
        "6.8.6.1p1", "a goto names a label in its function", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { goto x; }\n",
        "int a;\n"
        "void f(void) { goto x; x: ; }\n",
    },
    {
        "6.8.6.1p1", "a goto into a variably modified identifier's scope", CHECK_CONSTRAINT_ERROR, 0,
        "void f(int n)\n"
        "{\n"
        "    goto x;\n"
        "    { int b[n]; x: (void)b; }\n"
        "}\n",
        "void f(int n)\n"
        "{\n"
        "    { int b[n]; goto x; x: (void)b; }\n"
        "}\n",
    },
    {
        "6.8.6.2p1", "a continue inside a loop", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { continue; }\n",
        "int a;\n"
        "void f(void) { while (1) { continue; } }\n",
    },
    {
        "6.8.6.2p1", "a continue inside a switch but no loop", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(int x) { switch (x) { case 1: continue; } }\n",
        "int a;\n"
        "void f(int x) { for (;;) { switch (x) { case 1: continue; } } }\n",
    },
    {
        "6.8.6.3p1", "a break inside a loop or switch", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { break; }\n",
        "int a;\n"
        "void f(int x) { switch (x) { case 1: break; } do { break; } while (1); }\n",
    },
    {
        "6.8.6.4p1", "a void function returns no value", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "void f(void) { return 1; }\n",
        "int a;\n"
        "void f(void) { return; }\n",
    },
    {
        "6.8.6.4p1", "a void function returns a void expression", CHECK_CONSTRAINT_ACCEPT, 0,
        "void g(void);\n"
        "void f(void) { return g(); }\n",
        "void g(void);\n"
        "void f(void) { g(); return; }\n",
    },
    {
        "6.8.6.4p1", "a non-void function returns a value", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int f(void) { return; }\n",
        "int a;\n"
        "int f(void) { return 1; }\n",
    },
    {
        "6.9p2", "auto on an external declaration", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "auto int b;\n",
        "int a;\n"
        "static int b;\n",
    },
    {
        "6.9p2", "register on an external declaration", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "register int b;\n",
        "int a;\n"
        "extern int b;\n",
    },
    {
        "6.9p2", "register on an external function declaration", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "register int g(void);\n",
        "int a;\n"
        "extern int g(void);\n",
    },
    {
        "6.9p3", "one definition of an internal object", CHECK_CONSTRAINT_ERROR, 0,
        "static int x = 1;\n"
        "static int x = 2;\n",
        "static int x;\n"
        "static int x = 2;\n",
    },
    {
        "6.9p3", "one definition of an internal function", CHECK_CONSTRAINT_ERROR, 0,
        "static int g(void) { return 1; }\n"
        "static int g(void) { return 2; }\n",
        "static int g(void);\n"
        "static int g(void) { return 2; }\n",
    },
    {
        "6.9p3", "an internal function used but not defined", CHECK_CONSTRAINT_WARNING, 0,
        "static int g(void);\n"
        "int f(void) { return g(); }\n",
        "static int g(void);\n"
        "int f(void) { return sizeof(g()); }\n",
    },
    {
        "6.9.1p2", "a function definition's identifier has function type", CHECK_CONSTRAINT_ERROR, 0,
        "typedef int F(void);\n"
        "F g { return 1; }\n",
        "typedef int F(void);\n"
        "F g;\n"
        "int g(void) { return 1; }\n",
    },
    {
        "6.9.1p3", "a function definition's result type is complete", CHECK_CONSTRAINT_ERROR, 0,
        "struct S;\n"
        "struct S g(void) { }\n",
        "struct S { int x; };\n"
        "struct S g(void) { struct S s = { 1 }; return s; }\n",
    },
    {
        "6.9.1p4", "a function definition's storage class is extern or static", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "auto int g(void) { return 1; }\n",
        "int a;\n"
        "extern int g(void) { return 1; }\n"
        "static int h(void) { return 2; }\n",
    },
    {
        "6.9.1p4", "a function definition declared typedef", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "typedef int g(void) { return 1; }\n",
        "int a;\n"
        "typedef int g(void);\n",
    },
    {
        "6.9.1p5", "a definition's parameters are named", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int g(int) { return 1; }\n",
        "int a;\n"
        "int g(void) { return 1; }\n",
    },
    {
        "6.9.1p5", "no declaration list after a parameter type list", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(int x) int x; { return x; }\n",
        "int a;\n"
        "int g(x) int x; { return x; }\n",
    },
    {
        "6.9.1p6", "a declaration list declares only the parameters", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(x) int y; { return 0; }\n",
        "int a;\n"
        "int g(x, y) long y; int x; { return x + (int)y; }\n",
    },
    {
        "6.9.1p6", "a declaration list without an initializer", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(x) int x = 1; { return x; }\n",
        "int a;\n"
        "int g(x) register int x; { return x; }\n",
    },
    {
        "6.9.1p6", "a declaration list without a storage class", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int g(x) static int x; { return x; }\n",
        "int a;\n"
        "int g(x) const int x; { return x; }\n",
    },
    {
        "6.10.1p1", "an #if expression of floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#if 1.0\n"
        "#endif\n",
        "int a;\n"
        "#if 1 + 2 * 3 - (4 / 2) % 3 << 1\n"
        "#endif\n",
    },
    {
        "6.10.1p1", "an #if expression without an assignment", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#if A = 1\n"
        "#endif\n",
        "int a;\n"
        "#if A == 1 || !defined(A)\n"
        "#endif\n",
    },
    {
        "6.10.1p1", "an #if with an expression", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#if\n"
        "#endif\n",
        "int a;\n"
        "#if 0\n"
        "#endif\n",
    },
    {
        "6.10.1p1", "an #if expression's defined of an identifier", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#if defined(1)\n"
        "#endif\n",
        "int a;\n"
        "#if defined(X) || defined Y\n"
        "#endif\n",
    },
    {
        "6.10.1p4", "an #if expression's tokens are tokens", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#if 1 ` 2\n"
        "#endif\n",
        "int a;\n"
        "#if 1 ^ 2\n"
        "#endif\n",
    },
    {
        "6.10.2p1", "an #include of a file that exists", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#include \"missing-catalogue-header.h\"\n",
        "int a;\n"
        "#ifndef ONCE\n"
        "#define ONCE\n"
        "#include __FILE__\n"
        "#endif\n",
    },
    {
        "6.10.3p2", "an object-like macro redefined differently", CHECK_CONSTRAINT_WARNING, 0,
        "#define A 1\n"
        "#define A 2\n"
        "int a = A;\n",
        "#define A 1\n"
        "#define A 1\n"
        "int a = A;\n",
    },
    {
        "6.10.3p2", "a function-like macro redefined differently", CHECK_CONSTRAINT_WARNING, 0,
        "#define F(x) x\n"
        "#define F(y) y\n"
        "int a = F(1);\n",
        "#define F(x) (x)\n"
        "#define F(x) (x)\n"
        "int a = F(1);\n",
    },
    {
        "6.10.3p3", "white space after an object-like macro's name", CHECK_CONSTRAINT_WARNING, 0,
        "#define A+1\n"
        "int a = A;\n",
        "#define A +1\n"
        "int a = A;\n",
    },
    {
        "6.10.3p4", "as many arguments as a function-like macro's parameters", CHECK_CONSTRAINT_ERROR, 0,
        "#define F(a, b) a\n"
        "int x = F(1);\n",
        "#define F(a, b) a\n"
        "int x = F(1, );\n",
    },
    {
        "6.10.3p4", "no more arguments than a function-like macro's parameters", CHECK_CONSTRAINT_ERROR, 0,
        "#define F(a) a\n"
        "int x = F(1, 2);\n",
        "#define F(a, ...) a\n"
        "int x = F(1, 2);\n",
    },
    {
        "6.10.3p4", "more arguments than a variadic macro's named parameters", CHECK_CONSTRAINT_ACCEPT, 0,
        "#define F(a, ...) a\n"
        "int x = F(1);\n",
        "#define F(a, ...) a\n"
        "int x = F(1, 2, 3);\n",
    },
    {
        "6.10.3p4", "a function-like macro invocation is terminated", CHECK_CONSTRAINT_ERROR, 0,
        "#define F(a) a\n"
        "int x = F(1;\n",
        "#define F(a) a\n"
        "int x = F(1);\n",
    },
    {
        "6.10.3p5", "__VA_ARGS__ only in a variadic macro's replacement", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "#define A __VA_ARGS__\n",
        "int a;\n"
        "#define A(...) __VA_ARGS__\n",
    },
    {
        "6.10.3p6", "a macro's parameters are unique", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#define F(x, x) x\n",
        "int a;\n"
        "#define F(x, y) x y\n",
    },
    {
        "6.10.3.2p1", "# is followed by a parameter", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#define F(x) # y\n",
        "int a;\n"
        "#define F(x) # x\n",
    },
    {
        "6.10.3.3p1", "## at the start of a replacement list", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#define A ## b\n",
        "int a;\n"
        "#define A a ## b\n",
    },
    {
        "6.10.3.3p1", "## at the end of a replacement list", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#define F(x) x ##\n",
        "int a;\n"
        "#define F(x) x ## 1\n",
    },
    {
        "6.10.4p3", "a #line directive's file name is a character string literal", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#line 10 L\"x.c\"\n",
        "int a;\n"
        "#line 10 \"x.c\"\n",
    },
    {
        "6.10.4p3", "a #line directive's digit sequence", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "#line x\n",
        "int a;\n"
        "#line 20\n",
    },
    {
        "6.3.1.1p2", "the integer promotions", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "_Static_assert(_Generic((char)1 + (char)1, int: 1, default: 0) && _Generic((unsigned short)1 + (unsigned short)1, int: 1, default: 0) && _Generic(+(_Bool)1, int: 1, default: 0), \"promotions\");\n",
        "struct S { unsigned a : 16; unsigned b : 32; long c : 40; } s;\n"
        "_Static_assert(_Generic(s.a + 0, int: 1, default: 0) && _Generic(s.b + 0, unsigned: 1, default: 0) && _Generic(+(signed char)1, int: 1, default: 0), \"bit-field promotions\");\n",
    },
    {
        "6.3.1.1p2", "a promotion that does not keep its value", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((unsigned char)255 + (unsigned char)1 == 0, \"no wrap: the operands are int\");\n",
        "int a;\n"
        "_Static_assert((unsigned char)255 + (unsigned char)1 == 256, \"the operands are int\");\n",
    },
    {
        "6.3.1.2p1", "conversion to _Bool", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((_Bool)256 == 0, \"nonzero is 1\");\n",
        "int a;\n"
        "_Static_assert((_Bool)256 == 1 && (_Bool)0.5 == 1 && (_Bool)0 == 0 && (_Bool)-1 == 1, \"_Bool\");\n",
    },
    {
        "6.3.1.3p2", "conversion to an unsigned type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((unsigned char)-1 == 127, \"modulo\");\n",
        "int a;\n"
        "_Static_assert((unsigned char)-1 == 255 && (unsigned short)65537 == 1 && (unsigned)-2 == 4294967294u, \"modulo\");\n",
    },
    {
        "6.3.1.3p3", "conversion to a narrower signed type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((signed char)200 == 200, \"implementation-defined\");\n",
        "int a;\n"
        "_Static_assert((signed char)200 == -56 && (short)65535 == -1, \"modulo, as GCC and clang define it\");\n",
    },
    {
        "6.3.1.4p1", "conversion of a floating value to an integer truncates", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((int)2.9 == 3, \"truncation\");\n",
        "int a;\n"
        "_Static_assert((int)2.9 == 2 && (int)-2.9 == -2 && (unsigned)3.7 == 3 && (long long)1e18 == 1000000000000000000, \"truncation\");\n",
    },
    {
        "6.3.1.4p1", "conversion of a floating value out of an integer's range", CHECK_CONSTRAINT_ACCEPT, 0,
        "int a;\n"
        "int b = (int)1e10;\n",
        "int a;\n"
        "int b = (int)1e9;\n",
    },
    {
        "6.3.1.4p2", "conversion of an integer to a floating type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((double)9007199254740993 == 9007199254740994.0, \"rounding\");\n",
        "int a;\n"
        "_Static_assert((double)9007199254740993 == 9007199254740992.0 && (float)16777217 == 16777216.0f, \"round to nearest even\");\n",
    },
    {
        "6.3.1.5p1", "conversion between real floating types", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((float)0.1 == 0.1, \"rounding\");\n",
        "int a;\n"
        "_Static_assert((float)0.1 != 0.1 && (double)0.1f == 0.1f && (long double)0.5 == 0.5L, \"real floating conversions\");\n",
    },
    {
        "6.3.1.6p1", "conversion between complex types", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert(__real__ (_Complex float)(_Complex double)0.1 == 0.1, \"parts convert\");\n",
        "int a;\n"
        "_Static_assert(__real__ (_Complex float)(_Complex double)0.5 == 0.5f, \"parts convert\");\n",
    },
    {
        "6.3.1.7p2", "conversion of a complex value to a real type", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert((double)(1.0 + 2.0i) == 3.0, \"the imaginary part is discarded\");\n",
        "int a;\n"
        "_Static_assert((double)(1.0 + 2.0i) == 1.0 && __imag__ (_Complex double)1.0 == 0.0, \"real and complex\");\n",
    },
    {
        "6.3.1.8p1", "the usual arithmetic conversions", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert(_Generic(1u + 1L, unsigned long: 1, default: 0), \"long holds unsigned int's values\");\n",
        "int a;\n"
        "_Static_assert(_Generic(1u + 1L, long: 1, default: 0) && _Generic(1u + 1, unsigned: 1, default: 0) && _Generic(1UL + 1LL, unsigned long long: 1, default: 0) && _Generic(1.0f + 1, float: 1, default: 0) && _Generic(1.0f + 1.0, double: 1, default: 0) && _Generic('a' + 1L, long: 1, default: 0), \"usual arithmetic conversions\");\n",
    },
    {
        "6.3.1.8p1", "the usual arithmetic conversions of a signed and an unsigned operand", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "_Static_assert(-1 < 1u, \"-1 converts to unsigned\");\n",
        "int a;\n"
        "_Static_assert(-1 > 1u && -1L < 1u && (short)-1 < (unsigned short)1, \"unsigned wins only at its rank\");\n",
    },
    {
        "6.3.2.1p2", "lvalue conversion drops qualifiers", CHECK_CONSTRAINT_ERROR, 0,
        "const volatile int c;\n"
        "_Static_assert(_Generic(c, const volatile int: 1, default: 0), \"qualifiers kept\");\n",
        "const volatile int c;\n"
        "_Static_assert(_Generic(c, int: 1, default: 0), \"lvalue conversion\");\n",
    },
    {
        "6.3.2.1p3", "an array decays to a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int b[2];\n"
        "_Static_assert(_Generic(b, int[2]: 1, default: 0), \"no decay\");\n",
        "int b[2];\n"
        "_Static_assert(_Generic(b, int *: 1, default: 0) && sizeof b == 2 * sizeof(int) && sizeof &b == sizeof(void *), \"decay but under sizeof and &\");\n",
    },
    {
        "6.3.2.1p4", "a function designator decays to a pointer", CHECK_CONSTRAINT_ERROR, 0,
        "int g(void);\n"
        "_Static_assert(_Generic(g, int(void): 1, default: 0), \"no decay\");\n",
        "int g(void);\n"
        "_Static_assert(_Generic(g, int (*)(void): 1, default: 0) && _Generic(&g, int (*)(void): 1, default: 0), \"decay\");\n",
    },
    {
        "6.3.2.2p1", "the value of a void expression is not used", CHECK_CONSTRAINT_ERROR, 0,
        "void g(void);\n"
        "int b(void) { return g(); }\n",
        "void g(void);\n"
        "int b(void) { (void)1; g(); return 0; }\n",
    },
    {
        "6.3.2.3p1", "a pointer to void and a pointer to an object convert", CHECK_CONSTRAINT_ACCEPT, 0,
        "void *v;\n"
        "int *p;\n"
        "void f(void) { p = v; v = p; }\n",
        "void *v;\n"
        "const int *p;\n"
        "void f(void) { p = v; }\n",
    },
    {
        "6.3.2.3p3", "a null pointer constant", CHECK_CONSTRAINT_WARNING, 0,
        "int a;\n"
        "int *p = 1 - 1;\n",
        "int a;\n"
        "int *p = 0;\n"
        "int *q = (void *)0;\n",
    },
    {
        "6.3.2.3p5", "an integer converts to a pointer with a cast", CHECK_CONSTRAINT_ERROR, 0,
        "int a;\n"
        "int *p = 4096;\n",
        "int a;\n"
        "int *p = (int *)4096;\n",
    },
    {
        "6.3.2.3p8", "a function pointer converts to another with a cast", CHECK_CONSTRAINT_ERROR, 0,
        "int g(void);\n"
        "long (*p)(void) = g;\n",
        "int g(void);\n"
        "long (*p)(void) = (long (*)(void))g;\n",
    },
};

// A preprocessing directive's source (6.10), which the corpus check gives no place for a preprocessing error of, through
// the preprocessor's two kernels: its acceptance (no error and no fatal error) and its first error's line, read from the
// diagnostics' stream, must be clang's (`oracle`).
static void check_test_constraint_preprocess(CheckTest* test, const char* name, const char* source, const char* path, const CheckCorpusOracle* oracle)
{
    for (uint32_t p = 0; p < 2; p += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        char* text = NULL;
        size_t length = 0;
        FILE* stream = open_memstream(&text, &length);
        const PPOptions options = {
            .language = {
                .standard = 2017,
                .gnu = true
            },
            .no_standard_includes = true,
            .scalar = p == 0,
            .diagnostics = stream,
        };
        const bool ready = stream != NULL && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
        const bool ran = ready && pp_run_source(&pp, path, (const uint8_t*)source, (uint32_t)strlen(source));
        const bool rejected = !ran || pp.errors != 0 || pp.fatal;
        if (stream != NULL)
        {
            fflush(stream);
        }
        uint32_t line = 0;
        for (const char* at = text; rejected && at != NULL && *at != 0 && line == 0;)
        {
            const char* end = strchr(at, '\n');
            const char* error = strstr(at, ": error:");
            const char* fatal = strstr(at, ": fatal error:");
            const char* found = error != NULL && (end == NULL || error < end) ? error : fatal != NULL && (end == NULL || fatal < end) ? fatal : NULL;
            if (found != NULL)
            {
                // "path:LINE:COLUMN: error:": the line is the number before the column's.
                const char* column = found;
                while (column > at && column[-1] != ':')
                {
                    column -= 1;
                }
                const char* number = column > at ? column - 1 : column;
                while (number > at && number[-1] >= '0' && number[-1] <= '9')
                {
                    number -= 1;
                }
                line = (uint32_t)strtoul(number, NULL, 10);
            }
            at = end != NULL ? end + 1 : NULL;
        }
        char description[512];
        snprintf(description, sizeof(description), "constraint %s: %s from the %s kernels, first line %u (clang %s, %u)", name, rejected ? "rejected" : "accepted",
                 p == 0 ? "scalar" : "vectorized", line, oracle->rejected ? "rejects" : "accepts", oracle->line);
        check_test_expect(test, ready, "constraint preprocessing fixture initialization");
        check_test_expect(test, rejected == oracle->rejected && (!rejected || line == oracle->line), description);
        if (ready)
        {
            pp_release(&pp);
            intern_release(&identifiers);
        }
        if (stream != NULL)
        {
            fclose(stream);
        }
        free(text);
    }
}

static void check_test_constraints(CheckTest* test, const char* clang)
{
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-constraints-XXXXXX", temporary != NULL ? temporary : "/tmp");
    const bool made = mkdtemp(directory) != NULL;
    check_test_expect(test, made, "the constraint catalogue's directory");
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
        .quiet = true,
        .clang_features = true,
        .semantic_pragmas = true,
    };
    const char* arguments[] =
    {
        "--no-default-config",
        "--target=x86_64-linux-gnu",
        "-std=gnu17",
        "-nostdinc",
    };
    CheckCorpusTally tally = { 0 };
    uint32_t errors = 0;
    for (uint32_t i = 0; made && i < 2 * BUSTER_ARRAY_LENGTH(check_constraint_fixtures); i += 1)
    {
        const CheckConstraintFixture* fixture = &check_constraint_fixtures[i / 2];
        const bool violation = (i & 1) == 0;
        const bool expected = violation && fixture->outcome == CHECK_CONSTRAINT_ERROR;
        const char* source = violation ? fixture->violation : fixture->valid;
        char name[256];
        snprintf(name, sizeof(name), "%s %s, %s", fixture->clause, fixture->rule, violation ? "violation" : "valid twin");
        char path[4300];
        snprintf(path, sizeof(path), "%s/constraint_%u.c", directory, i);
        CheckCorpusOracle oracle;
        bool ready = check_corpus_write(path, source, strlen(source)) && check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), path, &oracle);
        char description[512];
        snprintf(description, sizeof(description), "constraint %s: clang %s, the fixture records %s", name, ready && oracle.rejected ? "rejects" : "accepts",
                 expected ? "an error" : "acceptance");
        check_test_expect(test, ready && oracle.rejected == expected, description);
        errors += expected;
        if (ready)
        {
            // A difference the checker keeps: it must reject the violation at its recorded line, where clang takes it.
            CheckCorpusOracle checked = oracle;
            if (violation && fixture->difference != 0)
            {
                checked.rejected = true;
                checked.line = fixture->difference;
                snprintf(checked.file, sizeof(checked.file), "%s", path);
            }
            if (strncmp(fixture->clause, "6.10", 4) == 0)
            {
                check_test_constraint_preprocess(test, name, source, path, &checked);
            }
            else
            {
                const bool different = check_corpus_file(&tally, path, name, "constraint", &options, &checked, NULL, 64);
                snprintf(description, sizeof(description), "constraint %s: the checker gives %s, on both paths alike", name,
                         violation && fixture->difference != 0 ? "its recorded difference" : "clang's result");
                check_test_expect(test, !different, description);
            }
            ready = unlink(path) == 0;
        }
        check_test_expect(test, ready, "the constraint fixture's file is removed");
    }
    printf("test-checker: constraint catalogue: %u fixtures, %u sources, %u of them errors\n", (uint32_t)BUSTER_ARRAY_LENGTH(check_constraint_fixtures),
           (uint32_t)(2 * BUSTER_ARRAY_LENGTH(check_constraint_fixtures)), errors);
    check_test_expect(test, !made || rmdir(directory) == 0, "the constraint catalogue's directory is removed");
}
