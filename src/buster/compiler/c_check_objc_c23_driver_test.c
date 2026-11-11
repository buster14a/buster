// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C23's spellings of Objective-C's pointer type attributes (#1447): [[clang::objc_gc]] and [[clang::objc_ownership]], by the
// driver against clang, on original fixtures. Each unit's whole output, by both paths, in GNU C17 and C23: after the
// specifiers, a pointer or a function's suffix, the type there alone; before a declaration or after a declarator's name, as
// GNU's after a declarator (check_input_attribute_objc), objc_gc's with -Wdeprecated-attributes' warning after the type's
// warnings and a typedef's error; their arguments, counts, macros and spellings; a declaration of no declarator; and
// GNU's garbage collection qualifier on a parameter, which the function's type drops. (clang 23 runs out of memory on
// [[clang::objc_gc(weak)]] before a declaration: no fixture.)

static void check_test_driver_objc_c23(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "int *[[clang::objc_gc(weak)]] p;\n",
        "int *[[clang::objc_gc(strong)]] p;\n",
        "int [[clang::objc_gc(weak)]] x;\n",
        "int *[[clang::objc_ownership(weak)]] p;\n",
        "int *[[clang::objc_ownership(strong)]] p;\n",
        "int *[[clang::objc_ownership(none)]] p;\n",
        "int *[[clang::objc_ownership(autoreleasing)]] p;\n",
        "int *[[clang::objc_ownership(bogus)]] p;\n",
        "int [[clang::objc_ownership(strong)]] x;\n",
        "[[clang::objc_ownership(strong)]] int *q;\n",
        "int *q [[clang::objc_ownership(strong)]];\n",
        "void f(void) [[clang::objc_ownership(strong)]];\n",
        "int *[[clang::objc_gc]] p;\n",
        "int *[[clang::objc_gc(1)]] p;\n",
        "int *[[clang::objc_gc(bogus)]] p;\n",
        "struct [[clang::objc_ownership(strong)]] S;\n",
        "void g(void) { [[clang::objc_gc(weak)]]; }\n",
        "int *[[clang::objc_gc(weak)]] p;\n" "int *__attribute__((objc_gc(weak))) p;\n",
        "int *[[clang::objc_gc(weak)]] p;\n" "int *p;\n",
        "int *[[clang::objc_ownership(weak)]] *pp;\n",
        "typedef int *[[clang::objc_ownership(strong)]] T;\n",
        "[[clang::objc_ownership(weak)]] int (*f)(int *);\n",
        "int (*f [[clang::objc_ownership(weak)]])(int *);\n",
        "[[clang::objc_ownership(weak)]] int **(*f)(void);\n",
        "int **(*f [[clang::objc_ownership(weak)]])(void);\n",
        "int x [[clang::objc_gc(weak)]];\n",
        "int *(*f [[clang::objc_gc(weak)]])(void);\n" "int *(*f)(void);\n",
        "int **q [[clang::objc_gc(weak)]];\n" "int **q;\n",
        "int *q [[clang::objc_gc(weak)]];\n",
        "int *q [[clang::objc_gc(strong)]], *r;\n" "int *r;\n",
        "[[clang::objc_ownership(weak)]] int x;\n",
        "int x [[clang::objc_ownership(weak)]];\n",
        "int [[clang::objc_ownership(weak)]] x;\n",
        "void f(void) [[clang::objc_ownership(weak)]];\n",
        "int x = sizeof(int [[clang::objc_ownership(weak)]]);\n",
        "__attribute__((objc_ownership(weak))) int x;\n",
        "int x __attribute__((objc_ownership(weak)));\n",
        "int x = sizeof(int *[[clang::objc_gc(strong)]]);\n",
        "[[clang::objc_ownership(strong)]];\n",
        "[[clang::objc_gc(weak)]];\n",
        "void f(int *[[clang::objc_gc(weak)]] p);\n" "void f(int *p);\n",
        "struct S { int *[[clang::objc_gc(weak)]] m; };\n",
        "typedef int *T [[clang::objc_gc(weak)]];\n",
        "int *[[clang::objc_gc(weak)]] [[clang::objc_gc(strong)]] p;\n",
        "int *[[clang::objc_gc(weak, strong)]] p;\n",
        "int *[[clang::objc_ownership(strong, weak)]] p;\n",
        "int *[[clang::objc_ownership(strong, zz)]] p;\n",
        "void g(void) { int *[[clang::objc_gc(weak)]] l; (void)l; }\n",
        "#define W [[clang::objc_ownership(weak)]]\n" "int *W p;\n",
        "int *[[clang::__objc_gc__(weak)]] p;\n" "int *p;\n",
        "int *[[_Clang::objc_ownership(none)]] p;\n",
        "void f(int *__attribute__((objc_gc(weak))) p);\n" "void f(int *p);\n",
        "void f(int *__attribute__((objc_gc(strong))) p);\n" "int x = _Generic(f, void (*)(int *): 1);\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/objc-c23.c", directory);
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
        snprintf(description, sizeof(description), "objc c23 %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "objc c23 %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
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
