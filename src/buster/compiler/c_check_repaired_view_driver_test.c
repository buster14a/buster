// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A unit whose parse error the checker reads through a repaired view (#2406, #2431, c_check_repair.c): the view's original
// tokens through its last complete statement, and synthetic closers for the bodies the error left open. Its common
// warnings are the view's complete original statements' (CheckCommonView), where the view has no other error
// (check_program's) and where it has (check_driver_rejected_common's readings): -Wunused-value, -Wunused-comparison,
// -Wparentheses, -Wstring-plus-int, -Wformat, -Wshift-count-overflow and -Wreturn-type, before the parse error in a body
// the original closes, after it in one the error left open. On original fixtures, the whole output, line for line, and
// the status, by the vector path, the scalar one and deferred bodies on one, two and three lanes, under gnu17 and gnu23,
// with -Wall and with -Wall -Wextra; and -c of each, which must be refused with no object written. The deferred readings
// are -c's, since -fsyntax-only starts no lanes. Include after c_check_structural_recovery_driver_test.c.

static void check_test_driver_repaired_view(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // A body the repair closes again before a file-scope parse error, and one the error leaves open: their warnings, the
        // deferred -Wunused-value at the body's end, before the error in the first and after it in the second.
        "void f(int a, int b) { a == b; a + 1; if (a = b) {} }\nint z = ;\n",
        "void f(int a, int b) { a == b; a + 1; if (a = b) {}\n",
        // -Wreturn-type of a complete function before the error, -Wformat and a shift count, none of the open body's.
        "int printf(const char *, ...);\nint g(void) { }\nint h(int a) { a == 1; printf(\"%s\", a); return a << 40; }\nint z = ;\n",
        // A body's error before the parse error (#2431): -Wstring-plus-int's warning and note with the -Wint-conversion error.
        "int n(int x) { return x + \"a\"; }\nvoid o(int a) { a = 1;\n",
        // The open body's complete statements' warnings, with a conversion error, a call to an undeclared function and a
        // jump to a label of another function in the bodies before it.
        "int printf(const char *, ...);\nvoid n(int x) { printf(\"%s\\n\", x); x << 40; char *p = x; (void)p; }\n"
        "void o(int a) { printf(\"%d\", 1.5); a == 2;\n",
        "void n(int x) { undeclared(x); x == 1; }\nvoid o(int a) { a == 2; if (a = 3) {}\n",
        "void n(int x) { goto L; x == 1; }\nvoid o(int a) { L: a == 2;\n",
        "int g(int a) { if (a) { a == 1; } else { a + 2; } return a + \"b\";\n",
        // A stray `)` in a body, after a complete statement the view keeps.
        "void g(int *p) { p == 0; }\nvoid h(int a) { a == 1; ) }\n",
        // Scope-pop warnings require a real closer (#2515): the inner block closes, the outer body does not.
        "void o(void) { int a = 1; { int b; }\n",
        "int n(int x) { return x + \"a\"; }\nvoid o(void) { int a = 1; { int b; }\n",
        // The repaired view's last original body closes before the later parse error, so its warning precedes it.
        "static int unused(void) { return 0; }\nvoid g(int a) { int b; a == 1; }\nint z = ;\n",
        "void g(int a) { int b; { int c; } a == 1; }\nint z = ;\n",
        "void g(void) { for (int i = 0; i < 1; ++i) { int b; } int a;\n",
        "void o(int p) { int a = 1; L:; { int b; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[][2] = {
        { "-Wall", NULL },
        { "-Wall", "-Wextra" },
        { "-Wall", "-Werror=unused-variable" },
        { "-Wall", "-Wno-unused-variable" },
        { "-Wall", "-Werror=unused-parameter" },
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/repaired-view.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/repaired-view-%u.c", directory, f);
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
                check_test_expect(test, lines && oracle != 0, "repaired view: clang rejects the fixture and its lines are read");
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
                    snprintf(description, sizeof(description), "repaired view %u, %s %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f,
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
            snprintf(description, sizeof(description), "repaired view %u, -c by the %s path: refused (status %d), no object", f,
                     scalar != 0 ? "scalar" : "vector", status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "repaired view: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
