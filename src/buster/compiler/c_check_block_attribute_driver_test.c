// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A block item's GNU attributes before an identifier (#147, #1447): clang's statement reader asks what the name is
// before it reads a declaration (TryAnnotateName), so a declared name of no type, or an undeclared one before `(`, is
// "expected identifier or '('" at it (the parser's parse_scalar_attribute_name), in any standard, and an undeclared one an
// implicit int's declarator. The attributes are a statement's only where each is one of clang's statement attributes,
// at least one (an empty list is a declaration's), after a control statement's head, or before a label's name, as the
// parser and the checker's check_input_statement_attributes both read them. The whole output, line for line, by both
// paths, against clang.
static void check_test_driver_block_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* source;
    } fixtures[] =
    {
        { "-std=gnu17", "int x;\nvoid f(void) {\n    __attribute__(()) x = 1;\n}\n" },
        { "-std=gnu17", "void g(void);\nenum { E };\nvoid f(int p) {\n    __attribute__((unused)) p = E;\n}\n" },
        { "-std=gnu17", "enum { E };\nvoid f(void) {\n    __attribute__(()) E;\n}\n" },
        { "-std=gnu17", "void g(void);\nvoid f(void) {\n    __attribute__((cold)) __attribute__(()) g();\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    switch (1) {\n    case 1: __attribute__(()) x = 1;\n    }\n}\n" },
        { "-std=gnu23", "int x;\nvoid f(void) {\n    int y = 1;\n    __attribute__(()) y = x;\n}\n" },
        { "-std=c89", "int x;\nvoid f(void) {\n    __attribute__(()) x = 1;\n}\n" },
        { "-std=c89", "void f(void) {\n    __attribute__(()) foo();\n}\n" },
        { "-std=c89", "int x, y;\nvoid g(void) { int z = 2; z = 3; }\nvoid f(void) {\n    __attribute__(()) x = y;\n}\n" },
        // Undeclared names, and a file-scope declaration after the body: implicit ints' declarations, which C89 takes.
        { "-std=c89", "void f(void) {\n    __attribute__(()) y = 1;\n    y = 2;\n}\n" },
        { "-std=gnu89", "void f(void) {\n    __attribute__((,)) y = 1;\n    __attribute__(()) __attribute__((unused)) z;\n    y = z;\n}\n" },
        { "-std=c89", "void g(void) { int z = 2; z = 3; }\nvoid f(void) {\n    __attribute__(()) w = 1;\n    w = 2;\n}\nint w;\n" },
        { "-std=gnu17", "void f(void) {\n    __attribute__(()) y = 1;\n}\n" },
        { "-std=gnu17", "typedef int T;\nint x;\nvoid f(void) {\n    __attribute__(()) T t = 1;\n    typedef int x;\n    { __attribute__(()) x u = t; (void)u; }\n}\n" },
        // Statements' attributes: after a control statement's head (where C has no declaration), before a label's name
        // (the label's), after a label (the label's), and clang's statement attributes alone.
        { "-std=gnu17", "int x;\nvoid f(void) {\n    if (1) __attribute__((unused)) x = 1;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    while (0) __attribute__((unused)) x = 1;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    if (1) ;\n    else __attribute__((unused)) x = 1;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    do __attribute__((unused)) x = 1; while (0);\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    for (;;) __attribute__((unused)) x = 1;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    if (1) __attribute__(()) x = 1;\n    for (;;) __attribute__(()) x = 2;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    __attribute__((unused)) a: x = 1;\n    __attribute__(()) b: x = 2;\n    __attribute__((cold)) c: ;\n}\n" },
        { "-std=gnu17", "int x;\nvoid f(void) {\n    l: __attribute__((unused)) x = 1;\n}\n" },
        { "-std=gnu17", "int g(void);\nvoid f(void) {\n    __attribute__((nomerge)) g();\n    __attribute__(()) __attribute__((nomerge)) g();\n}\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/block-attribute.c", directory);
    enum { CAPACITY = 16384 };
    char expected_lines[CAPACITY];
    char actual_lines[CAPACITY];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const bool written = parse_test_write(source, fixtures[f].source, strlen(fixtures[f].source));
        char* arguments[] = { (char*)clang, (char*)fixtures[f].standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, CAPACITY);
        check_test_expect(test, written && lines && oracle >= 0, "block attribute fixture and oracle available");
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, CAPACITY) && strcmp(expected_lines, actual_lines) == 0;
            char description[4096];
            snprintf(description, sizeof(description), "block attribute %u (%s), path %u: status %d (Clang %d), lines\n%s(Clang\n%s)", f,
                     fixtures[f].standard, scalar, status, oracle, actual != NULL ? actual_lines : "unavailable", expected_lines);
            check_test_expect(test, status >= 0 && same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    check_test_expect(test, unlink(source) == 0, "block attribute fixture removed");
}
