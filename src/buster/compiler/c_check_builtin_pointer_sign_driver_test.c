// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a builtin call's argument pointer warnings (#2189 item 3, c_checker_driver.c's
// check_driver_pointer_arguments): -Wpointer-sign and -Wincompatible-pointer-types-discards-qualifiers in clang's words,
// "passing 'T' to parameter of type 'U' ...", at the argument, with no parameter's note, for builtins whose arguments are a
// list (`__builtin_strcmp`) or operands (`__builtin_strcpy`). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c. Also #2189's other items: CFString's
// argument of no ordinary literal (check_semantic_expression), dump_struct's rest argument that doesn't convert to its
// printer's parameter with clang's two notes (check_driver_dump_struct), and verbose_trap through a const pointer whose
// own initializer lost the designator (check_input_verbose_trap_object). And #2460: a builtin's argument that doesn't
// convert, in the conversion's words (check_driver_builtin_conversion).

static const CheckDriverDefaultWarningFixture check_driver_builtin_pointer_sign_fixtures[] =
{
    {
        .name = "builtin arguments' pointer warnings",
        .source = "int g(unsigned char *p) { return __builtin_strlen(p) + __builtin_strcmp(p, \"x\") + __builtin_strncmp(\"y\", p, 1); }\n"
                  "void h(unsigned char *d, const char *s, const unsigned char *cu) { __builtin_strcpy(d, s); __builtin_strcpy((char *)cu, cu); __builtin_strcat(d, cu); }\n"
                  "void k(const char *c) { __builtin_strcpy(c, \"x\"); __builtin_memset(c, 0, 1); }\n"
                  "#define S(x) __builtin_strlen(x)\n"
                  "unsigned long m(signed char *sp) { return S(sp); }\n",
        .warnings = 10,
        .notes = 0,
    },
    {
        .name = "CFString's argument of no ordinary literal (item 1)",
        .source = "static const char s[] = \"x\";\n"
                  "const void* d = __builtin___CFStringMakeConstantString(s);\n"
                  "void f(int x, const char *p) { (void)__builtin___CFStringMakeConstantString(p); (void)__builtin___CFStringMakeConstantString((const char*)\"a\"); }\n"
                  "void g(void) { (void)__builtin___CFStringMakeConstantString((\"a\")); (void)__builtin___CFStringMakeConstantString(u8\"a\"); }\n",
        .warnings = 0,
        .errors = 3,
        .notes = 0,
    },
    {
        .name = "dump_struct's rest argument that doesn't convert to its printer's parameter (item 2)",
        .source = "struct S { int a; };\n"
                  "union U { int a; };\n"
                  "typedef struct { int a; } T;\n"
                  "int p(int* x, const char* f, ...);\n"
                  "int q(int y, int* x, const char* f, ...);\n"
                  "int r(long* x, const char* f, ...);\n"
                  "void g(struct S* s, double z) { __builtin_dump_struct(s, p, z); }\n"
                  "void h(union U* s, struct S v) { __builtin_dump_struct(s, &q, 1 + 2, v); }\n"
                  "void k(T* s, int *ip) { __builtin_dump_struct(s, p, 5); }\n"
                  "void m(struct S* s, int (*fp)(int*, const char*, ...)) { __builtin_dump_struct(s, fp, 1.5); }\n"
                  "void n(struct S* s, int *ip) { __builtin_dump_struct(s, r, ip); }\n",
        .warnings = 0,
        .errors = 5,
        .notes = 9,
    },
    {
        .name = "verbose_trap through a const pointer whose initializer lost its designator (item 4)",
        .source = "static const char arr[] = \"hello\";\n"
                  "static const char* const q = (const char*)(const void*)arr;\n"
                  "static const char* const q2 = arr;\n"
                  "static const char* const q3 = (const char*)(const void*)\"lit\";\n"
                  "static const char* const q4 = (const char*)arr, *const q5 = (const char*)(void*)arr;\n"
                  "struct R { char m[8]; };\n"
                  "static const struct R r = { \"member\" };\n"
                  "static const char* const q6 = (const char*)(const void*)r.m;\n"
                  "void f1(void) { __builtin_verbose_trap(\"c\", q); }\n"
                  "void f2(void) { __builtin_verbose_trap(\"c\", q2); }\n"
                  "void f3(void) { __builtin_verbose_trap(\"c\", q3); }\n"
                  "void f4(void) { __builtin_verbose_trap(\"c\", q4); }\n"
                  "void f5(void) { __builtin_verbose_trap(\"c\", q5); }\n"
                  "void f6(void) { __builtin_verbose_trap(\"c\", q6); }\n"
                  "void f7(void) { __builtin_verbose_trap(\"c\", (const char*)(const void*)q2); }\n",
        .warnings = 0,
        .errors = 4,
        .notes = 0,
    },
    {
        // A builtin's argument that doesn't convert to its parameter (#2460, check_driver_builtin_conversion): the
        // conversion's own words at the argument, no parameter's note; CFString's then adds its literal's error.
        .name = "builtin arguments that don't convert",
        .source = "struct S { int a; } s;\n"
                  "void g(void) { (void)__builtin_strlen(1); }\n"
                  "void h(void) { (void)__builtin_strlen(L\"a\"); }\n"
                  "void k(void) { (void)__builtin___CFStringMakeConstantString(1); }\n"
                  "void m(char *p, int *ip) { (void)__builtin_strlen(s); (void)__builtin_memcpy(p, 1, 2); "
                  "(void)__builtin___CFStringMakeConstantString(ip); }\n",
        .warnings = 0,
        .errors = 8,
        .notes = 0,
    },
};

static void check_test_driver_builtin_pointer_sign(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_builtin_pointer_sign_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_builtin_pointer_sign_fixtures));
}
