// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The parser's "to match this '('" notes (#147), by the driver against clang, by both paths of -fsyntax-only: the whole
// output, line for line, with the exit status. An expected closer's error at a token has clang's note at the innermost
// group of its kind (a group's, a call's, a subscript's, a condition's, sizeof's, a keyword's, an initializer's); the
// builtins clang reads without a balanced tracker (__builtin_va_arg's) and a GNU attribute's list have none.
static void check_test_driver_parse_notes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "int f(int x) { return (x + 1; }\n",
        "int g(int a[3) { return 0; }\n",
        "int h(int x) { return h(1; }\n",
        "void k(void) { while (1 ; }\n",
        "int m(int x) { return sizeof(int; }\n",
        "struct S { int a; }; int n(void) { return __builtin_offsetof(struct S, a; }\n",
        "int q(int x) { return _Generic(x, int: 1; }\n",
        "int r[] = { 1, 2 ;\n",
        "int y(int (*p)(int x); \n",
        "int v(int n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n); int r = __builtin_va_arg(ap, int; return r; }\n",
        "void w(void) __attribute__((noreturn);\n",
        "int x; void f(void) { __attribute__(()) x = 1; }\n",
        "void f(int x) { __attribute__((unused)) x; }\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/parse-notes.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const bool made = parse_test_write(source, units[unit], strlen(units[unit]));
        char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "parse notes %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "parse notes %u (%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, units[unit],
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
