// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery (#1447): type attributes' errors and shared alignments.
// - A vector_size or neon_vector_type error leaves the type as it was (clang's HandleVectorSizeAttr and
//   HandleNeonVectorTypeAttr). A mode error leaves it too, or installs the mode's type all the same where clang's
//   AddModeAttr does (a mode of the other kind, or a base of no integer or floating type). An enum's mode whose error
//   installs nothing leaves the enum without it.
// - An aligned's or _Alignas's value error, and a specifiers' mode error, is given again for each later declarator that
//   shares it, as clang's handlers run for each (CheckInputAttributeRepeat).
// The checker goes on to the declaration and the unit's later ones (check_input_attribute_type). On original fixtures,
// the whole output, line for line, and the status, by the vector path, the scalar one and deferred bodies on two lanes,
// under gnu17 and gnu23 and the warning and error-limit options; and -c of each, which must be refused with no object
// written. Include after c_check_semantic_recovery_driver_test.c, whose reader this takes.

static void check_test_driver_type_attribute_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // Each vector_size error at a variable, with the declaration's type (its element's) and a later error.
        "int v1 __attribute__((vector_size(3)));\nint v2 __attribute__((vector_size(0)));\nint v3 __attribute__((vector_size(-4)));\n"
        "int n; int v4 __attribute__((vector_size(n)));\nint *v5 __attribute__((vector_size(16)));\n_Bool v6 __attribute__((vector_size(16)));\n"
        "int v7 __attribute__((vector_size(1ull << 62)));\nstruct S { int a; } v8 __attribute__((vector_size(16)));\n"
        "struct Z *q1 = &v1;\nstruct Z *q5 = &v5;\nstruct Z *q8 = &v8;\nint after = \"a\";\n",
        // A typedef's, a member's, a parameter's, a block's, a type name's, and two declarators' own errors.
        "typedef int V __attribute__((vector_size(3)));\nV v;\nstruct Z *q = &v;\n"
        "struct T { int m __attribute__((vector_size(3))); int k; };\nstruct Z *qm = &((struct T *)0)->m;\n"
        "int f(int __attribute__((vector_size(3))) x);\n"
        "void g(void) { int b __attribute__((vector_size(3))); struct Z *qb = &b; int x = sizeof(int __attribute__((vector_size(0)))); }\n"
        "int w1 __attribute__((vector_size(3))), w2 __attribute__((vector_size(0)));\nstruct Z *qw = &w2;\n"
        "enum { K = sizeof(int __attribute__((vector_size(3)))) };\n_Static_assert(sizeof(int __attribute__((vector_size(3)))) == 4, \"x\");\n",
        // A specifiers' vector_size, applied once; with an initializer, and with other attributes in its list.
        "__attribute__((vector_size(3))) int a, b;\nstruct Z *qa = &a;\nint i __attribute__((vector_size(3))) = 4;\n"
        "int j __attribute__((vector_size(3), aligned(3)));\nint k __attribute__((vector_size(0), unused, section(1)));\nstruct Z *qk = &k;\n",
        // neon_vector_type's element and size.
        "int n1 __attribute__((neon_vector_type(3)));\nint *n2 __attribute__((neon_vector_type(4)));\nstruct Z *q = &n1;\nint after = \"a\";\n",
        // Each mode error: the type kept, or the mode's installed.
        "int m1 __attribute__((mode(QQ)));\nfloat m2 __attribute__((mode(SI)));\nint m3 __attribute__((mode(SF)));\n"
        "struct S { int a; } m4 __attribute__((mode(SI)));\nint *m5 __attribute__((mode(SI)));\nint m6 __attribute__((mode(XC)));\n"
        "double _Complex m7 __attribute__((mode(SI)));\nint m8 __attribute__((mode(TF)));\nlong double m9 __attribute__((mode(IF)));\n"
        "int m10 __attribute__((mode(1)));\n"
        "struct Z *q1 = &m1;\nstruct Z *q2 = &m2;\nstruct Z *q3 = &m3;\nstruct Z *q4 = &m4;\nstruct Z *q5 = &m5;\nstruct Z *q6 = &m6;\n"
        "struct Z *q7 = &m7;\nstruct Z *q8 = &m8;\nstruct Z *q9 = &m9;\nint after = \"a\";\n",
        // Vector modes and vector bases: a deprecated mode's warning first, a complex mode of a vector's second error.
        "enum E { A } e1 __attribute__((mode(V4SI)));\nfloat e2 __attribute__((mode(V4SI)));\nstruct S { int a; } e3 __attribute__((mode(V2DF)));\n"
        "typedef int V4 __attribute__((vector_size(16)));\nV4 e4 __attribute__((mode(SC)));\n"
        "typedef float F4 __attribute__((vector_size(16)));\nF4 e5 __attribute__((mode(SI)));\n"
        "struct Z *q1 = &e1;\nstruct Z *q2 = &e2;\nstruct Z *q4 = &e4;\nstruct Z *q5 = &e5;\n",
        // Mode in other places: a member, a parameter, a typedef of an enum, a function pointer, a type name, a block.
        "struct T { int m __attribute__((mode(QQ))); int k; };\nstruct Z *qm = &((struct T *)0)->m;\n"
        "void f(int p __attribute__((mode(SF)))) { struct Z *q = &p; int b __attribute__((mode(QQ))); (void)b; }\n"
        "typedef enum { A } E __attribute__((mode(SF)));\nstruct Z *qe = (E *)0;\nint (*fp)(void) __attribute__((mode(SI)));\n"
        "int x = sizeof(int __attribute__((mode(QQ))));\nint after = \"a\";\n",
        // An enum's mode that installs nothing, before and after its body; the enum is as without it.
        "enum __attribute__((mode(QQ))) E { A };\nstruct Z *q1 = (enum E *)0;\nenum F { B } __attribute__((mode(QQ)));\n"
        "struct Z *q2 = (enum F *)0;\nenum __attribute__((mode(V4SI))) G { C };\nenum __attribute__((mode(IF))) H { D };\n"
        "_Static_assert(sizeof(enum E) == 4 && sizeof(enum F) == 4, \"enum\");\nint after = \"a\";\n",
        // A specifiers' mode error, each declarator's; a parameter's, a member's and a typedef's.
        "__attribute__((mode(QQ))) int a, b;\nstruct Z *qb = &b;\n__attribute__((mode(SF))) int c, d, e;\nstruct Z *qe = &e;\n"
        "__attribute__((mode(IF))) long double g, h;\nvoid f(__attribute__((mode(QQ))) int p, __attribute__((mode(QQ))) int r);\n"
        "struct S { __attribute__((mode(QQ))) int m, n; };\ntypedef __attribute__((mode(SF))) int T, U;\nstruct Z *qu = (U *)0;\n",
        // A shared alignment's error, each declarator's: _Alignas and aligned, at file and block scope, members and
        // storage classes, two in one list, two lists, and a declarator's own after the shared one.
        "_Alignas(3) int a1, b1, c1;\nint y = sizeof a1;\n__attribute__((aligned(3))) int a2, b2;\nint a3 __attribute__((aligned(3))), b3;\n"
        "_Alignas(-1) int a4, b4;\n_Alignas(3) _Alignas(5) int a5, b5;\n__attribute__((aligned(3))) __attribute__((aligned(5))) int a6, b6;\n"
        "__attribute__((aligned(3))) int a7, b7 __attribute__((aligned(5)));\n_Alignas(3) static int a8, b8;\nint _Alignas(3) a9[2], *b9;\n",
        "struct S { _Alignas(3) int a, b; __attribute__((aligned(3))) int c, d : 3, e; };\n"
        "void f(void) { _Alignas(3) int a = \"x\", b; _Alignas(3) static int c, d; extern _Alignas(3) int e, g; }\n"
        "_Alignas(3) int i = 1, j = \"x\";\n_Alignas(3) int k; _Alignas(3) int l, m, o;\n"
        "__attribute__((aligned(3), section(1))) int p, r;\n__attribute__((aligned(3), nonnull(1))) int s, t;\nint after = \"a\";\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { NULL, "-Wall", "-ferror-limit=2", "-w" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/type-attribute-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/type-attribute-recovery-%u.c", directory, f);
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
                check_test_expect(test, lines && oracle != 0, "type attribute recovery: clang rejects the fixture and its lines are read");
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
                    snprintf(description, sizeof(description), "type attribute recovery %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f,
                             standards[s], options[o] != NULL ? options[o] : "", mode, status, oracle, actual_lines, expected_lines);
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
            snprintf(description, sizeof(description), "type attribute recovery %u, -c by the %s path: refused (status %d), no object", f,
                     scalar != 0 ? "scalar" : "vector", status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "type attribute recovery: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
