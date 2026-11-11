// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// cfi_salt (#1447), part of a prototype's type, by the driver against clang, on original fixtures. Each unit's whole output,
// by both paths, in GNU C17 and C23: salted and unsalted functions and function pointers in redeclarations, assignments,
// initializers, _Generic and calls, typedefs, members and parameters, two salts on one type, empty and concatenated
// salts, K&R functions' error, the attribute's places in either syntax (the function type's warning, its subjects' error,
// C23's "cannot be applied to a declaration") and its arguments' errors. Two types that differ by their salt alone print
// alike, as clang's do.

static void check_test_driver_cfi_salt(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "void f(void) __attribute__((cfi_salt(\"a\"), cfi_salt(\"b\")));\n" "void f(void) __attribute__((cfi_salt(\"b\")));\n",
        "void f(void) __attribute__((cfi_salt(\"a\"))) __attribute__((cfi_salt(\"a\")));\n" "void f(void) __attribute__((cfi_salt(\"a\")));\n",
        "void (*p)(void) __attribute__((cfi_salt(\"a\")));\n" "void (*q)(void);\n" "void g(void) { q = p; }\n",
        "void (*p)(void) __attribute__((cfi_salt(\"a\")));\n" "void (*q)(void) __attribute__((cfi_salt(\"a\")));\n" "void g(void) { q = p; }\n",
        "void (*p)(void) __attribute__((cfi_salt(\"a\")));\n" "int x = _Generic(p, void (*)(void): 1, default: 2);\n" "_Static_assert(_Generic(p, void (*)(void): 0, default: 1), \"\");\n",
        "void f(void) [[clang::cfi_salt(\"a\")]];\n" "void f(void);\n",
        "[[clang::cfi_salt(\"a\")]] void f(void);\n",
        "void f(void) __attribute__((cfi_salt(\"\")));\n" "void f(void);\n",
        "void f(void) __attribute__((cfi_salt(\"a\" \"b\")));\n" "void f(void) __attribute__((cfi_salt(\"ab\")));\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "void (*p)(void) = f;\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "void g(void) { f(); }\n",
        "typedef void F(void) __attribute__((cfi_salt(\"a\")));\n" "F f;\n" "void f(void) __attribute__((cfi_salt(\"a\")));\n",
        "void f() __attribute__((cfi_salt(\"a\")));\n",
        "int __attribute__((cfi_salt(\"a\"))) *x;\n",
        "struct S { void (*m)(void) __attribute__((cfi_salt(\"a\"))); } s;\n" "void (*q)(void);\n" "void g(void) { q = s.m; }\n",
        "void f(void (*cb)(void) __attribute__((cfi_salt(\"a\"))));\n" "void h(void);\n" "void g(void) { f(h); }\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "int x = sizeof(f == f);\n",
        "void (* __attribute__((cfi_salt(\"a\"))) p)(void);\n" "void (*q)(void);\n" "void g(void) { q = p; }\n",
        "void f() __attribute__((cfi_salt(\"a\")));\n" "void f();\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "void f();\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "void f(void) {}\n",
        "void f(int) __attribute__((cfi_salt(\"a\")));\n" "void f(int x) __attribute__((cfi_salt(\"a\"))) { }\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n",
        "void f(void) __attribute__((cfi_salt(1)));\n",
        "void f(void) __attribute__((cfi_salt));\n",
        "void f(void) __attribute__((cfi_salt(\"a\", \"b\")));\n",
        "int x __attribute__((cfi_salt(\"a\")));\n",
        "typedef void F(void) __attribute__((cfi_salt(\"a\")));\n" "F *p;\n" "void (*q)(void) __attribute__((cfi_salt(\"a\")));\n" "void g(void) { q = p; p = q; }\n",
        "struct S { int a; } __attribute__((cfi_salt(\"a\")));\n",
        "void (*a[2])(void) __attribute__((cfi_salt(\"a\")));\n" "void (*b[2])(void);\n" "void g(void) { b[0] = a[0]; }\n",
        "int *__attribute__((cfi_salt(\"a\"))) x;\n",
        "void g(int x __attribute__((cfi_salt(\"a\"))));\n",
        "struct S { int m __attribute__((cfi_salt(\"a\"))); };\n",
        "typedef int T __attribute__((cfi_salt(\"a\")));\n",
        "__attribute__((cfi_salt(\"a\"))) int x;\n",
        "__attribute__((cfi_salt(\"a\"))) void f(void);\n" "void f(void);\n",
        "void f(void) __attribute__((cfi_salt(\"a\")));\n" "void (*g(void))(void) __attribute__((cfi_salt(\"a\")));\n",
        "void (*g(void) __attribute__((cfi_salt(\"a\"))))(void);\n" "void (*g(void))(void);\n",
        "int x = sizeof(void (*)(void) __attribute__((cfi_salt(\"a\"))));\n",
        "void (*p)(void) [[clang::cfi_salt(\"a\")]];\n" "void (*q)(void);\n" "void g(void) { q = p; }\n",
        "void (*p [[clang::cfi_salt(\"a\")]])(void);\n",
        "int [[clang::cfi_salt(\"a\")]] x;\n",
        "typedef void F(void) [[clang::cfi_salt(\"a\")]];\n" "F f;\n" "void f(void);\n",
        "void f(void) [[clang::cfi_salt(\"a\")]] [[clang::cfi_salt(\"b\")]];\n" "void f(void) [[clang::cfi_salt(\"b\")]];\n",
        "void (*p [[clang::nonblocking]])(void);\n",
        "struct S { void (*m [[clang::cfi_salt(\"a\")]])(void); };\n",
        "void g(void (*p [[clang::cfi_salt(\"a\")]])(void));\n",
        "int x [[clang::nonblocking]];\n",
        "int x [[clang::cfi_salt(\"a\")]];\n",
        "void f [[clang::cfi_salt(\"a\")]] (void);\n",
        "int *p [[clang::cfi_salt(\"a\")]];\n",
        "void (**p [[clang::cfi_salt(\"a\")]])(void);\n",
        "void (*a [[clang::cfi_salt(\"a\")]] [2])(void);\n",
        "typedef void (*T [[clang::cfi_salt(\"a\")]])(void);\n",
        "int x [[clang::cfi_salt(\"a\")]], y(void);\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/cfi-salt.c", directory);
    const uint32_t total = BUSTER_ARRAY_LENGTH(units) * BUSTER_ARRAY_LENGTH(standards);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const char* text = units[unit % BUSTER_ARRAY_LENGTH(units)];
        char* standard = (char*)standards[unit / BUSTER_ARRAY_LENGTH(units)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "cfi salts %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "cfi salts %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
