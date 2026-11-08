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
        // The same on declarators with parameters, whose subject checks come before their function's (#1764's review): the
        // function's Sema errors still wait for its declarator's end, and a parameter's for its own.
        "__attribute__((aligned(3), section(1))) void f(int *p);\n__attribute__((aligned(3), section(1))) int g(int a, int b);\n"
        "__attribute__((aligned(3), section(1))) void (*fp)(int);\nvoid h(__attribute__((aligned(3), section(1))) int p);\n"
        "__attribute__((nonnull(5), section(1))) void k(int *p);\nint m(void) { return missing_name; }\n",
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
        // Phase 2: a redeclaration that conflicts with the visible declaration is dropped, its name's later uses finding
        // the earlier one, with clang's wording and note for each kind of conflict.
        "int a;\nfloat a;\nint b = 1;\ndouble b = 2;\nextern int c;\nextern float c;\nextern int d;\nfloat d;\n"
        "int use(void) { return a + b + c + d; }\nfloat *p = &a;\n",
        "typedef int T;\ntypedef float T;\nT v;\nint T2;\ntypedef int T2;\nint f(void);\nint f;\nint w = sizeof(T);\n",
        "int f(int);\nint f(double);\nint g(int) { return 0; }\nint g(double);\nint h(int);\ndouble h(int x) { int u; return x << 40; }\n"
        "static int s;\nint s;\nint k = 1;\nint k = 2;\nint call(void) { return f(1) + g(2) + h(3); }\n",
        // A block's, and clang's unused-variable warnings of no scope in which an error occurred (a body's from its
        // definition's start).
        "void f(int p)\n{\n    int x;\n    float x;\n    int y = 1;\n    long y = 2;\n    (void)y;\n    if (y = 2) {}\n}\n"
        "void g(void) { int x; int x; }\nvoid h(void) { extern int e; extern float e; }\nvoid i(void) { int unused; }\n",
        "__attribute__((nonnull(5))) void f(int *p) { int u; }\nvoid g(void) { int v; { int w; } }\n",
        // A repeated enumerator, dropped: the next one's implicit value restarts at 0 (of int, or of a fixed type), and
        // an enum left with none is unsigned int's.
        "enum A { X = 5 };\nenum B { Y = 3, X, Z };\n_Static_assert(Z == 0, \"z\");\n"
        "enum C : long { P };\nenum D : char { Q = 7, P, R };\n_Static_assert(R == 0 && sizeof(R) == 1, \"r\");\n"
        "enum E { M = -1 };\nenum F { M };\n_Static_assert(sizeof(enum F) == 4 && (enum F)-1 > 0, \"u\");\nint x = undeclared;\n",
        // A tag defined again in its scope, whose body becomes an anonymous tag's: later uses of the name find the first
        // definition, and the second's enumerators are still declared (a repeated one with its own note).
        "struct S { int a; };\nstruct S { long b; };\nunion U { int a; };\nunion U { char c; } u2;\n"
        "typedef struct T { int x; } T;\nstruct T { int y; } t2;\n"
        "int f(void) { struct S s; union U u; s.a = 1; u.c = 1; return s.a + t2.y + undeclared; }\n",
        "enum E { A, B };\nenum E { C, A };\nint g(void) { return C + B; }\n"
        "void k(void)\n{\n    struct L { int a; };\n    { struct L { int b; }; }\n    struct L { int c; } v;\n    int unused;\n    v.c = 0;\n}\n"
        "void m(void) { int quiet; }\n",
        // The note is at the definition when a forward declaration or a use comes first (review on #1788).
        "struct S;\nstruct S { int a; };\nstruct S { int b; };\nstruct S { int c; };\nstruct T x;\nstruct T { int a; };\n"
        "struct T { int b; };\nenum E *pe;\nenum E { A };\nenum E { B };\n"
        "void f(void) { struct L *p; struct L { int a; }; struct L { int b; }; (void)p; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    // The tag redefinitions, the last three fixtures, are gnu17's alone: C23 allows a compatible one and words an
    // incompatible one otherwise (N3037), which the checker doesn't model yet.
    const uint32_t before_c23 = BUSTER_ARRAY_LENGTH(fixtures) - 3;
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
        const uint32_t standard_count = f >= before_c23 ? 1u : (uint32_t)BUSTER_ARRAY_LENGTH(standards);
        for (uint32_t s = 0; made && s < standard_count; s += 1)
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
