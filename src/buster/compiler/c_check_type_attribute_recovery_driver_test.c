// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery (#1447): type attributes' errors and shared alignments.
// - A vector_size or neon_vector_type error leaves the type as it was (clang's HandleVectorSizeAttr and
//   HandleNeonVectorTypeAttr). A mode error leaves it too, or installs the mode's type all the same where clang's
//   AddModeAttr does (a mode of the other kind, or a base of no integer or floating type). An enum's mode whose error
//   installs nothing leaves the enum without it; one that installs a floating type gives it the integer of its layout.
// - An aligned's value error, and a specifiers' mode error, is given again for each later declarator that shares it, as
//   clang's handlers run for each (CheckInputAttributeRepeat).
// - Each _Alignas error is each declarator's, in clang's order (check_input_alignas_declarator): its place's (a function's,
//   a typedef's, a parameter's, a bit-field's, a register variable's), else its value's (no power of 2, no integer
//   constant, past 2^32), then the underalignment, at the last _Alignas that stands. A declaration that declares nothing
//   has none.
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
        "void f(void) { _Alignas(3) int a = \"x\", b; _Alignas(3) static int c, d; extern _Alignas(3) int e, g; __attribute__((aligned(3))) int h, i; }\n"
        "_Alignas(3) int i = 1, j = \"x\";\n_Alignas(3) int k; _Alignas(3) int l, m, o;\n"
        "__attribute__((aligned(3), section(1))) int p, r;\n__attribute__((aligned(3), nonnull(1))) int s, t;\nint after = \"a\";\n",
        // _Alignas on a function or a typedef, the subject's error for each _Alignas and declarator, a variable's value error after.
        "_Alignas(3) int f(void), b;\n"
        "_Alignas(3) _Alignas(8) int g(void), c;\n"
        "typedef _Alignas(3) int T, U;\n"
        "typedef _Alignas(8) int V;\n"
        "_Alignas(3) int a, h(void), d;\n"
        "_Alignas(3) int k(void) { return 0; }\n"
        "struct Z *q = &b;\n",
        // _Alignas on a parameter, a bit-field and a register variable: the place's error for each, before the value's.
        "void g(_Alignas(3) int p);\n"
        "void g2(_Alignas(8) int p, _Alignas(8) _Alignas(16) int q);\n"
        "int f(_Alignas(3) int);\n"
        "struct S { _Alignas(3) int a : 2, b; _Alignas(8) int c : 2, d : 3; };\n"
        "void h(void) { _Alignas(3) register int r, s; _Alignas(8) register int t; int u = r + s + t; struct Z *q = &u; }\n",
        // _Alignas's and aligned's values of no integer constant and past 2^32, for each declarator; a typedef's and a parameter's place first.
        "int n;\n"
        "_Alignas(n) int a, b;\n"
        "_Alignas(1ull << 40) int c, d;\n"
        "_Alignas(1.5) int e;\n"
        "__attribute__((aligned(1ull << 40))) int g, h;\n"
        "typedef _Alignas(n) int T, U;\n"
        "void p(_Alignas(n) int x);\n"
        "struct Z *q = &a;\n"
        "struct Z *r = &c;\n",
        // Underalignment after the declarator's _Alignas values' errors, at the last _Alignas that stands.
        "_Alignas(1) int u, v;\n"
        "_Alignas(1) _Alignas(3) int w, x;\n"
        "_Alignas(3) _Alignas(1) int y;\n"
        "_Alignas(1) _Alignas(2) int z;\n"
        "_Alignas(1) int _Alignas(2) z2;\n"
        "struct S { _Alignas(1) int a, b; _Alignas(1) _Alignas(2) int c; };\n"
        "void f(void) { _Alignas(1) _Alignas(3) int m = 1, n; struct Z *q = &n; }\n",
        // Declarations that declare nothing take no _Alignas value error; an anonymous record member and a for's declarators do.
        "_Alignas(3) int;\n"
        "_Alignas(3) struct S { int a; };\n"
        "struct T { _Alignas(3) int; _Alignas(1) int; int b; _Alignas(3) struct { int x; }; };\n"
        "void f(void) { for (_Alignas(3) int i = 0, j; i; i++) ; }\n"
        "_Alignas(3) _Thread_local int t, u;\n"
        "struct Z *q = &t;\n",
        // An enum's floating mode, of the other kind: clang installs the floating type as its integer type, here the integer
        // of its layout (check_input_mode_enum_integer), before and after its body.
        "enum __attribute__((mode(SF))) E { A, B = A + 1 };\n_Static_assert(sizeof(enum E) == 4 && _Alignof(enum E) == 4, \"four\");\n"
        "enum E e = B;\nstruct Z *q = &e;\nenum F { C } __attribute__((mode(DF)));\n"
        "_Static_assert(sizeof(enum F) == 8 && _Alignof(enum F) == 8, \"eight\");\nenum F f = C;\nstruct Z *s = &f;\n"
        "enum __attribute__((mode(XF))) G { D };\n_Static_assert(sizeof(enum G) == 16 && _Alignof(enum G) == 16, \"sixteen\");\n"
        "enum __attribute__((mode(HF))) I { I0 };\n_Static_assert(sizeof(enum I) == 2, \"hf\");\nint k = C << 40;\nstruct Z *t = (enum I *)0;\n",
        // An enum's complex mode (#1447): clang installs the complex type as its integer type, here the integer of its size
        // with the complex type's alignment (check_input_mode_enum_integer), but where an alignment attribute gives one,
        // before and after its body; its enumerators stay int. (Clang crashes on arithmetic with such an enum's object.)
        "enum __attribute__((mode(SC))) E { A, B = A + 1 };\n_Static_assert(sizeof(enum E) == 8 && _Alignof(enum E) == 4, \"sc\");\n"
        "_Static_assert(_Generic(B, int: 1, default: 0), \"int\");\nenum E e;\nstruct Z *q = &e;\nenum F { C } __attribute__((mode(DC)));\n"
        "_Static_assert(sizeof(enum F) == 16 && _Alignof(enum F) == 8, \"dc\");\nenum __attribute__((mode(SC), aligned(2))) J { J1 };\n"
        "_Static_assert(sizeof(enum J) == 8 && _Alignof(enum J) == 2, \"aligned\");\nenum K { K1 } __attribute__((aligned(16), mode(SC)));\n"
        "_Static_assert(sizeof(enum K) == 8, \"late aligned\");\nstruct L { char c; enum E m; };\n_Static_assert(sizeof(struct L) == 12, \"member\");\n"
        "int use(void) { int unused; return C + J1 + K1 + B; }\n",
        // XC's and TC's complex type (32 bytes), which no integer stands in for (#1447): its error recovers, the enum one
        // with no mode, before and after its body.
        "typedef int X __attribute__((mode(XC)));\nenum __attribute__((mode(XC))) E { A };\nenum __attribute__((mode(TC))) F { B };\n"
        "enum G { C } __attribute__((mode(XC)));\nint use(void) { int unused; return A + B + C + undeclared; }\n",
        // Conversions into such an enum: a floating value's silent (no -Wliteral-conversion on an enumeration target), an
        // integer's -Wconstant-conversion at its integer's width, as clang's; no other error, so the common warnings run.
        "enum __attribute__((mode(SF))) E { A };\nenum E e = 2.5;\nenum E g = 5000000000;\nenum E h = 1e20;\n"
        "void f(enum E x, double d) { enum E y = d; x = 3.5; e = 1ll << 40; (void)x; (void)y; }\n"
        "enum F { B } __attribute__((mode(DF)));\nenum F k = 1e30;\nenum F m = 1ll << 40;\nint after = B + A;\n",
        // A tag's misplaced attribute list (#2468), which its declaration reads past: a later constant that reads the tag
        // (_Alignof, sizeof, an array bound, an enumerator) keeps the unit's later warnings, at file scope and in a body.
        "struct U [[gnu::aligned(8)]] { int a; };\n_Static_assert(_Alignof(struct U) == 8, \"\");\nint y = 1/0;\n",
        "struct U [[gnu::aligned(8)]] { int a; };\nint arr[sizeof(struct U)];\nenum { E = sizeof(struct U) };\nint y = 1/0;\n"
        "void f(int a) { a == 1; }\n",
        "struct V [[gnu::packed]] { char c; int i; };\n_Static_assert(sizeof(struct V) == 8, \"\");\nint g(int a) { return a << 40; }\n"
        "int z = 1 << 40;\n",
        // Last, gnu17's alone (C23 has no K&R definition): a K&R definition's parameter declarations' _Alignas, which the
        // parser takes as the function declarator's suffix, each declarator's "cannot be applied to a function parameter".
        "void kr(a) _Alignas(3) int a; {}\nvoid kr2(a, b) _Alignas(8) int a, b; { (void)a; (void)b; }\n"
        "void kr3(a, b) int a; _Alignas(3) _Alignas(8) int b; { (void)a; }\nint kr4(a) _Alignas(8) register int a; { return a; }\n"
        "int n;\nvoid kr5(a) _Alignas(n) int a; { (void)a; }\n"
        // An invalid K&R definition, and a K&R declaration list's declarator errors (#1447): the definition's body is still
        // checked, the function invalid; a listed parameter's error recovers, the parameter an invalid stand-in.
        "int kf(a)[3] int a; { return 0; }\nint kg(b) int b[-1]; { int *q = b; (void)q; return 0; }\n"
        "int kh(c, d) int c; int (d)(void)[2]; { return c; }\nint kc = kf(1) + kg(2) + kh(3, 0);\nint kx = undeclared;\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    // The K&R definitions, the last fixture, are gnu17's alone.
    const uint32_t before_c23 = BUSTER_ARRAY_LENGTH(fixtures) - 1;
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
