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

// The legacy preprocessor omits #warning's option suffix (the initializer/constant timing controls have the same
// boundary). Normalize only this fixture's explicit lookahead marker; its location, severity, message and ordering,
// and every semantic diagnostic's full text and option label, remain exact.
static void check_test_semantic_recovery_lookahead(char* text)
{
    static const char marker[] = ": warning: after-loop";
    static const char labeled[] = ": warning: after-loop [-W#warnings]";
    char* match = text != NULL ? strstr(text, labeled) : NULL;
    if (match != NULL)
    {
        const char* end = match + sizeof(labeled) - 1;
        if (*end == '\n' || *end == 0)
        {
            memmove(match + sizeof(marker) - 1, end, strlen(end) + 1);
        }
    }
}

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
        // An object's type conflict takes precedence over a TLS mismatch (#1784), with the new declaration's extern
        // deciding "redeclaration" rather than "redefinition". Earlier tentative, extern and initialized declarations.
        "_Thread_local int type_tls;\nfloat type_tls;\n"
        "int type_plain;\n_Thread_local float type_plain;\n"
        "extern _Thread_local int type_tls_extern;\nextern float type_tls_extern;\n"
        "extern int type_plain_extern;\nextern _Thread_local float type_plain_extern;\n"
        "_Thread_local int type_tls_defined = 1;\nfloat type_tls_defined;\n"
        "int type_plain_defined = 1;\n_Thread_local float type_plain_defined;\n",
        // TLS-only conflicts in both directions, after a tentative definition, an extern and an initialized definition.
        "_Thread_local int tls_tentative;\nint tls_tentative;\n"
        "int plain_tentative;\n_Thread_local int plain_tentative;\n"
        "extern _Thread_local int tls_declared;\nextern int tls_declared;\n"
        "extern int plain_declared;\nextern _Thread_local int plain_declared;\n"
        "_Thread_local int tls_defined = 1;\nextern int tls_defined;\n"
        "int plain_defined = 1;\nextern _Thread_local int plain_defined;\n",
        // GNU and C23 TLS spellings use the same diagnostic family; compatible declarations and local shadowing remain
        // valid controls in the same translation unit. GNU spelling keeps this fixture erroneous under both standards.
        "extern __thread int gnu_consistent;\n__thread int gnu_consistent;\n"
        "void shadow(void) { int gnu_consistent = 0; (void)gnu_consistent; }\n"
        "__thread int gnu_tls;\nint gnu_tls;\nint gnu_plain;\n__thread int gnu_plain;\n"
        "#if __STDC_VERSION__ >= 202311L\nthread_local int keyword_tls;\nint keyword_tls;\n"
        "int keyword_plain;\nthread_local int keyword_plain;\n#endif\n",
        // Linkage conflicts and duplicate block-scope definitions precede TLS-only conflicts. An explicit extern
        // inherits the earlier static linkage; a simultaneous object-type conflict precedes both linkage and TLS.
        "int static_after_plain;\nstatic _Thread_local int static_after_plain;\n"
        "_Thread_local int static_after_tls;\nstatic int static_after_tls;\n"
        "static int external_after_plain;\n_Thread_local int external_after_plain;\n"
        "static _Thread_local int external_after_tls;\nint external_after_tls;\n"
        "static int extern_after_plain;\nextern _Thread_local int extern_after_plain;\n"
        "static _Thread_local int extern_after_tls;\nextern int extern_after_tls;\n"
        "int type_before_static;\nstatic _Thread_local float type_before_static;\n"
        "static _Thread_local int type_before_external;\nfloat type_before_external;\n"
        "void local_plain(void) { static int local; static _Thread_local int local; (void)local; }\n"
        "void local_tls(void) { static _Thread_local int local; static int local; (void)local; }\n",
        // Type conflicts combined with TLS mismatches do not replace the binding or become the next conflict's note.
        // Compatible declarations and definitions still merge, and later address expressions retain the original type.
        "extern _Thread_local int tls_chain;\nextern float tls_chain;\nextern double tls_chain;\n"
        "extern _Thread_local int tls_chain;\nfloat tls_chain;\n_Thread_local int tls_chain = 3;\n"
        "extern _Thread_local int tls_chain;\nfloat tls_chain;\n"
        "int plain_chain;\nextern _Thread_local float plain_chain;\nextern _Thread_local double plain_chain;\n"
        "extern int plain_chain;\n_Thread_local float plain_chain;\nint plain_chain = 4;\n"
        "extern int plain_chain;\n_Thread_local float plain_chain;\n"
        "int *use_tls(void) { return &tls_chain; }\nint *use_plain(void) { return &plain_chain; }\n",
        // A type conflict combined with a TLS mismatch still notes the latest compatible declaration of the linked
        // object, even when that extern declaration is in another function's block. A TLS-only conflict instead notes
        // the declaration its lookup sees, here the tentative definition at file scope.
        "_Thread_local int block_tls;\n"
        "void declare_tls(void) { extern _Thread_local int block_tls; (void)block_tls; }\n"
        "void conflict_tls(void) { extern float block_tls; (void)block_tls; }\n"
        "int block_plain;\n"
        "void declare_plain(void) { extern int block_plain; (void)block_plain; }\n"
        "void conflict_plain(void) { extern _Thread_local float block_plain; (void)block_plain; }\n"
        "_Thread_local int visible_tls;\n"
        "void declare_visible_tls(void) { extern _Thread_local int visible_tls; (void)visible_tls; }\nint visible_tls;\n"
        "int visible_plain;\n"
        "void declare_visible_plain(void) { extern int visible_plain; (void)visible_plain; }\n_Thread_local int visible_plain;\n"
        "int *use_block_tls(void) { return &block_tls; }\nint *use_block_plain(void) { return &block_plain; }\n",
        // Every declaration's failed initializer, not only the first's (#1447, phase 2): arrays, records and pointers, at
        // file and block scope, a static object's braced conversion, and an error of the walk's between them.
        "struct P { int x; int y; };\nint a[2] = 5;\nstruct P p = { .z = 1 };\nvoid f(void)\n{\n    int b[2] = 6;\n    struct P q = 3;\n"
        "    char c[1] = 7;\n    (void)b; (void)q; (void)c;\n}\nstatic int *sp = { 1.5 };\nint n = undeclared;\nstruct P r = { 1, 2, 3 };\nint e[3] = 8;\n",
        "int k;\nstatic int s1 = k;\nint a[2] = 5;\nstruct Q { char *p; } qs = { 4 };\nstatic struct Q qq = { 5L };\nint u(void) { int w; return 0; }\n",
        // A block's, and clang's unused-variable warnings of no scope in which an error occurred (a body's from its
        // definition's start).
        "void f(int p)\n{\n    int x;\n    float x;\n    int y = 1;\n    long y = 2;\n    (void)y;\n    if (y = 2) {}\n}\n"
        "void g(void) { int x; int x; }\nvoid h(void) { extern int e; extern float e; }\nvoid i(void) { int unused; }\n",
        "__attribute__((nonnull(5))) void f(int *p) { int u; }\nvoid g(void) { int v; { int w; } }\n",
        // A for initializer's scope ends with the whole loop, so a later error cannot suppress its unused warning.
        "void unbraced(void) { for (int j; ; ) break; int x; float x; }\n"
        "void braced(void) { for (int j; ; ) { break; } int x; float x; }\n",
        // An earlier error lies outside the new for scope, while an error in its body suppresses its warning.
        "void before(void) { int x; float x; for (int j; ; ) break; }\n"
        "void inside(void) { for (int j; ; ) { int x; float x; break; } }\n",
        // Nested controls and do/while bodies finish at their whole statement's end, not the header or first brace.
        "void nested(int n) { for (int i; ; ) for (int j; ; ) if (n) break; else break; int x; float x; }\n"
        "void trailing(void) { for (int j; ; ) do { break; } while (0); int x; float x; }\n",
        // The body's scope pops before the for initializer's at their shared completion. Preprocessing lookahead keeps
        // its earlier position at that boundary, and multiple declarations in one scope retain declaration order.
        "void body(void) { for (int j, m; ; ) { int k, n; break; } int x; float x; }\n"
        "void lookahead(void) {\nfor (int j; ; ) { int k; break; }\n#warning after-loop\nint x; float x;\n}\n",
        // A statement expression has its own inner scope; its error is also within the enclosing for scope.
        "void compound(void) { for (int j = ({ int k; 0; }); ; ) break; int x; float x; }\n"
        "void compound_error(void) { for (int j = ({ int k; int x; float x; 0; }); ; ) break; }\n",
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
                check_test_semantic_recovery_lookahead(expected);
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
                    check_test_semantic_recovery_lookahead(actual);
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
