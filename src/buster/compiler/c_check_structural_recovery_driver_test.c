// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A rejected unit's view parts around the errors #1801's view couldn't leave out alone (#1447,
// check_driver_rejected_edit): a condition's, which `-*__func__` replaces; a for statement's clause; a statement
// expression's value, whose error is its expression's; and a declaration's other than through an initializer it can do
// without, whose declarator is left out with its later uses, or whose const scalar keeps its declaration and an unknown
// value. Each fixture has errors mid-body, with clang's warnings before and after them in the same body. Where a part
// can't be left out (it holds a label or a goto), its body is left out whole, and the other bodies keep their warnings.
// On original fixtures, the whole output, line for line, and the status, by the vector path, the scalar one and deferred
// bodies on one, two and three lanes, under gnu17 and gnu23, with -Wall and with -Wall -Wextra; and -c of each, which
// must be refused with no object written. The deferred readings are -c's, since -fsyntax-only starts no lanes. Include
// after c_check_semantic_recovery_driver_test.c.

static void check_test_driver_structural_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // Conditions: an if's, a while's, a do's and a switch's, among -Wparentheses, -Wempty-body, -Wformat, -Wswitch,
        // -Wself-assign, -Wsign-compare and a shift count in the same body; the condition's statement keeps its body.
        "struct S { int a; } s;\n"
        "enum E { A, B };\n"
        "int printf(const char *, ...);\n"
        "int conditions(int x, unsigned u, enum E e)\n"
        "{\n"
        "    if (x = 1) x = 0;\n"
        "    if (x);\n"
        "    printf(\"%s\\n\", x);\n"
        "    switch (e) { case A: break; }\n"
        "    if (s.b) x = x;\n"
        "    if (x = 2) x = 0;\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    while (x < s.b) { if (x = 3) x = 0; }\n"
        "    do { x = x; } while (s.b);\n"
        "    switch (s.b) { case 1: if (x = 4) x = 0; break; }\n"
        "    if (s.b);\n"
        "    switch (e) { case A: break; }\n"
        "    if (x < u) return x << 40;\n"
        "    return 0;\n"
        "}\n"
        "int after(int y) { if (y = 5) return 1; return y; }\n",
        // A for statement's clauses: the init's, the condition's (replaced, so the loop doesn't become endless) and the
        // increment's; an init declaration's initializer, and a const one's.
        "struct S { int a; } s;\n"
        "int printf(const char *, ...);\n"
        "int loops(int x, unsigned u)\n"
        "{\n"
        "    for (x = s.b; x < u; x++) { if (x = 1) x = 0; }\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    for (x = 0; x < s.b; x++) { x = x; }\n"
        "    for (; x < u; x += s.b) { if (x = 2) x = 0; }\n"
        "    for (int i = s.b; i < u; i++) { x = x; }\n"
        "    for (const int c = s.b; x < c; x++) { if (x = 3) x = 0; }\n"
        "    if (x);\n"
        "    return x << 40;\n"
        "}\n",
        // Statement expressions: an error in a statement before the last leaves the value; in the last, the expression
        // around it is the error's, in a statement and in a condition.
        "struct S { int a; } s;\n"
        "int printf(const char *, ...);\n"
        "int expressions(int x, unsigned u)\n"
        "{\n"
        "    if (x = 1) x = 0;\n"
        "    x = ({ x = x; int t = s.b; t << 40; });\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    x = ({ int q = 1; q + s.b; });\n"
        "    if (x = 2) x = 0;\n"
        "    if (({ int v = 2; v + s.b; })) x = 0;\n"
        "    if (x < u) return x;\n"
        "    return 0;\n"
        "}\n",
        // Declarations: a const scalar's initializer, whose object stays usable; an array of unknown size's and a const
        // pointer's, whose declarators are left out with their uses; the declaration's other declarators kept.
        "struct S { int a; } s;\n"
        "enum E { A, B };\n"
        "int printf(const char *, ...);\n"
        "int declarations(int x, unsigned u, enum E e)\n"
        "{\n"
        "    if (x = 1) x = 0;\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    const int c = s.b, k = 2;\n"
        "    x = x;\n"
        "    if (x = c) x = k;\n"
        "    int y = c + (x << 40);\n"
        "    switch (e) { case A: break; }\n"
        "    int a[] = s.b, n = 3, m[] = { 1, 2 };\n"
        "    if (x < u) return a[0] + n;\n"
        "    const char *p = s.b, *r = \"r\";\n"
        "    if (x = 3) return *p;\n"
        "    printf(\"%s\\n\", x);\n"
        "    return y + m[0] + *r;\n"
        "}\n",
        // Every declarator of a declaration in error, first and last; and the first and the middle of three.
        "struct S { int a; } s;\n"
        "int printf(const char *, ...);\n"
        "int lists(int x)\n"
        "{\n"
        "    const char *p = s.b, *q = s.b;\n"
        "    x = x;\n"
        "    const char *f = s.b, *g = s.b, *h = \"h\";\n"
        "    if (x = 1) return *h;\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    return 0;\n"
        "}\n",
        // The fallback: a part that holds a goto or a label can't be left out, so its body is left out whole; the other
        // bodies keep their warnings, and their own errors' parts are left out alone.
        "struct S { int a; } s;\n"
        "int printf(const char *, ...);\n"
        "int fallback(int x)\n"
        "{\n"
        "    x = ({ if (x) goto out; s.b; });\n"
        "out:\n"
        "    return x;\n"
        "}\n"
        "int labelled(int x)\n"
        "{\n"
        "    x = ({ __label__ in; in: s.b; });\n"
        "    return x;\n"
        "}\n"
        "int other(int y, unsigned u)\n"
        "{\n"
        "    if (y = 1) y = y;\n"
        "    while (s.b) printf(\"%d\\n\", 1.5);\n"
        "    if (y < u) return y << 40;\n"
        "    return 0;\n"
        "}\n",
        // A set parameter whose read the view leaves out gets no -Wunused-but-set-parameter, as clang gives none in a
        // function with an error; nor does one an expression statement's error leaves unread, while the next function's does.
        "struct S { int a; } s;\n"
        "void f(int p, int q) { p = 1; q = 2; if (q + s.b) return; }\n"
        "void g(int p, int q) { p = 1; q = 2; s.b; }\n"
        "void h(int p) { p = 1; }\n",
        // A file-scope const object whose initializer has an error keeps its declaration, as clang's stays valid, so the
        // bodies that use it keep their warnings (#1447).
        "int printf(const char *, ...);\n"
        "struct P { int a; int b; };\n"
        "static const int s = undeclared;\n"
        "const int c = undeclared + 1, e = 2;\n"
        "const struct P p = { undeclared, 2 };\n"
        "const double d = undeclared;\n"
        "int g(int z) { int w; z = z; printf(\"%s\", z); return z + s + c + e + p.a + (int)d; }\n"
        "int h(int z) { int v; z = z; return z << 40; }\n"
        // A fixed-size const array's and a const pointer's (#1447, review of #1956).
        "static const int a[2] = { undeclared, 1 };\n"
        "int *const cp = &undeclared2;\n"
        "int k(int x) { int w; x = x; printf(\"%s\", x); return x + a[1] + *cp; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[][2] = { { "-Wall", NULL }, { "-Wall", "-Wextra" } };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/structural-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/structural-recovery-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* arguments[12] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                // The deferred readings' arguments: -c's, of no object (-fsyntax-only starts no lanes).
                char* compile[16] = { (char*)executable, (char*)standards[s], "-c", "-o", object, "-fno-caret-diagnostics", source };
                uint32_t compile_count = 7;
                for (uint32_t a = 0; a < 2 && options[o][a] != NULL; a += 1)
                {
                    arguments[count++] = (char*)options[o][a];
                    compile[compile_count++] = (char*)options[o][a];
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "structural recovery: clang rejects the fixture and its lines are read");
                // The vector path, the scalar one, and deferred bodies on 1, 2 and 3 lanes.
                static const char* const mode_lanes[] = { NULL, NULL, "1", "2", "3" };
                for (uint32_t mode = 0; lines && mode < BUSTER_ARRAY_LENGTH(mode_lanes); mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t at = count;
                    char** spawned = arguments;
                    if (mode == 1)
                    {
                        arguments[at++] = "--scalar";
                        arguments[at] = NULL;
                    }
                    else if (mode_lanes[mode] != NULL)
                    {
                        at = compile_count;
                        compile[at++] = "--deferred-bodies";
                        compile[at++] = "--lanes";
                        compile[at++] = (char*)mode_lanes[mode];
                        compile[at] = NULL;
                        spawned = compile;
                        unlink(object);
                    }
                    else
                    {
                        arguments[at] = NULL;
                    }
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(spawned, &actual);
                    const bool written = spawned == compile && access(object, F_OK) == 0;
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "structural recovery %u, %s %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f,
                             standards[s], options[o][0], options[o][1] != NULL ? options[o][1] : "", mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0) && !written, description);
                    free(actual);
                }
                free(expected);
            }
        }
        // -c is refused, and writes no object, by both paths.
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            unlink(object);
            char* arguments[] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, scalar != 0 ? "--scalar" : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool written = access(object, F_OK) == 0;
            char description[256];
            snprintf(description, sizeof(description), "structural recovery %u, -c by the %s path: refused (status %d), no object", f,
                     scalar != 0 ? "scalar" : "vector", status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "structural recovery: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
