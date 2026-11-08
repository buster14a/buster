// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery, phase 1 (#1447): an attribute's error that clang's handleDeclAttribute
// gives and then drops the attribute for leaves its declaration valid, so the checker goes on to the declaration's other
// attributes, the declaration itself and the unit's later declarations (check_input_attribute_recover, CheckInput's
// recovered reports). On original fixtures, the whole output, line for line, and the status, by the vector path, the
// scalar one and deferred bodies on two lanes, under gnu17 and gnu23 and the warning and error-limit options; and -c of
// each, which must be refused with no object written. Include after c_check_resume_driver_test.c, whose reader this
// takes.

static void check_test_driver_semantic_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // patchable_function_entry's offset past its count, on separate declarations, twice in one list, and on a shared
        // record, which each declarator's handler refuses again (#1734).
        "__attribute__((patchable_function_entry(1, 2))) void f(void) {}\n"
        "__attribute__((patchable_function_entry(3, 4))) void g(void) {}\n"
        "__attribute__((patchable_function_entry(3, 4), patchable_function_entry(1, 9))) void h(void) {}\n"
        "__attribute__((patchable_function_entry(1, 2))) void p(void), q(void);\n"
        "int use(void) { f(); g(); h(); p(); q(); return 0; }\n",
        // Each of the attribute errors' passes: an item's reading (a string's), its arguments' values, its subject, its
        // function type's and its place on a definition; the declarations stay usable.
        "__attribute__((section(1))) int a1;\n"
        "__attribute__((aligned(3))) int a2;\n"
        "__attribute__((noreturn(1))) void a3(void);\n"
        "__attribute__((nonnull(5))) void a4(int *p);\n"
        "__attribute__((format(printf, 1, 2))) void a5(int x, ...);\n"
        "__attribute__((alloc_size(3))) void *a6(int n);\n"
        "__attribute__((cleanup(a6))) int a7;\n"
        "__attribute__((warn_unused_result)) int a8;\n"
        "__attribute__((constructor(\"x\"))) void a9(void);\n"
        "__attribute__((visibility(1))) int a10;\n"
        "__attribute__((alias)) int a11;\n"
        "int ok(void) { return a1 + a2 + a10; }\n",
        // A body's warnings after the errors, and no unit-end warning (an unused static's or function's) once there is one.
        "__attribute__((section(1))) int a1;\n"
        "static int unused_static;\n"
        "static void unused_function(void) {}\n"
        "int f(int x)\n"
        "{\n"
        "    int unused_local;\n"
        "    if (x = 3) return 1;\n"
        "    return x;\n"
        "}\n"
        "__attribute__((nonnull(5))) void g(int *p);\n",
        // Two errors in one list, the parser's (a string's) before Sema's (an alignment's) as clang gives them, and the
        // attribute between them kept.
        "__attribute__((aligned(3), unused, section(1))) int x;\nint y = sizeof x;\n",
        // A recovered error, then one that ends the unit's checking as before (a control's).
        "__attribute__((nonnull(5))) void g(int *p);\nvoid k(void) { break; }\n",
        // Recovered errors with missing names, whose constructs alone are cut (check_input_diagnostic_recover).
        "__attribute__((alias)) int a;\nint b = y;\nint c = sizeof a;\nint d = z;\n",
        // A block's declarations, which source order checks where a lane would have read the body.
        "void f(void)\n{\n    __attribute__((aligned(3))) int x;\n    int y;\n    if (y = 1) {}\n"
        "    __attribute__((section(1))) static int z;\n    (void)x; (void)z;\n}\n",
        "void a(void) { }\nvoid f(void)\n{\n    __attribute__((aligned(3))) int x;\n    int y = 0;\n    (void)x;\n    if (y = 1) {}\n}\n"
        "void g(void) { int u; }\nstatic void h(void) {}\n",
        // A mutual exclusion, the later attribute's error, on one list and across two.
        "__attribute__((cold, hot)) void f(void);\n__attribute__((hot)) __attribute__((cold)) void g(void);\nint h(void) { f(); g(); return 0; }\n",
        // An alignment specifier's.
        "_Alignas(3) int x;\n_Alignas(5) int y;\nint z = sizeof(x) + sizeof(y);\n",
        // A dropped format, whose calls aren't checked against it.
        "__attribute__((format(printf, 1, 2))) void a5(int x, ...);\n__attribute__((alloc_size(3))) void *a6(int n);\n"
        "__attribute__((constructor(\"x\"))) void a9(void);\n__attribute__((visibility(1))) int a10;\nvoid u(void) { a5(1); a6(2); }\n",
        // An enumerator's attribute of no enumerator's subject.
        "enum G { G0 __attribute__((mode(SI))), G1 __attribute__((mode(SI))) };\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { NULL, "-Wall", "-ferror-limit=2", "-w" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/semantic-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/semantic-recovery-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* arguments[12] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                if (options[o] != NULL)
                {
                    arguments[count++] = (char*)options[o];
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "semantic recovery: clang rejects the fixture and its lines are read");
                for (uint32_t mode = 0; lines && mode < 3; mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t at = count;
                    if (mode == 1)
                    {
                        arguments[at++] = "--scalar";
                    }
                    else if (mode == 2)
                    {
                        arguments[at++] = "--deferred-bodies";
                        arguments[at++] = "--lanes";
                        arguments[at++] = "2";
                    }
                    arguments[at] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "semantic recovery %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f, standards[s],
                             options[o] != NULL ? options[o] : "", mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0), description);
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
            snprintf(description, sizeof(description), "semantic recovery %u, -c by the %s path: refused (status %d), no object", f, scalar != 0 ? "scalar" : "vector",
                     status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "semantic recovery: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
