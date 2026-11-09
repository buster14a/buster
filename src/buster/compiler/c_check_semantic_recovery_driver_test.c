// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery, phase 1 (#1447): an attribute's error that clang's handleDeclAttribute
// gives and then drops the attribute for leaves its declaration valid, so the checker goes on to the declaration's other
// attributes, the declaration itself and the unit's later declarations (check_input_attribute_recover, CheckInput's
// recovered reports). On original fixtures, the whole output, line for line, and the status, by the vector path, the
// scalar one and deferred bodies (whose bodies carry their recovered reports through the lanes) on one, two and three
// lanes, under gnu17 and gnu23 and the warning and error-limit options; and -c of each, which must be refused with no
// object written. The deferred readings are -c's, since -fsyntax-only starts no lanes; the driver reads --scalar's in
// source order, and test-checker-deferred checks the deferred reader's scalar twins. Include after c_check_resume_driver_test.c, whose reader this
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


// The typo stress fixture's source (#1447): three thousand file-scope names (alpha_000 to charlie_999) declared one by one,
// and a function that reads fifty-three of them misspelled, each without its `_`, which only its own name is one edit from.
// Its length, or 0 where `capacity` is too small.
static uint64_t check_test_typo_stress(char* text, uint64_t capacity)
{
    static const char* const prefixes[] = { "alpha", "bravo", "charlie" };
    uint64_t length = 0;
    bool fits = true;
    for (uint32_t p = 0; fits && p < BUSTER_ARRAY_LENGTH(prefixes); p += 1)
    {
        for (uint32_t i = 0; fits && i < 1000; i += 1)
        {
            const int written = snprintf(text + length, capacity - length, "int %s_%03u;\n", prefixes[p], i);
            fits = written > 0 && (uint64_t)written < capacity - length;
            length += fits ? (uint64_t)written : 0;
        }
    }
    const int head = fits ? snprintf(text + length, capacity - length, "int f(void)\n{\n    int s = 0;\n") : -1;
    fits = head > 0 && (uint64_t)head < capacity - length;
    length += fits ? (uint64_t)head : 0;
    for (uint32_t k = 0; fits && k < 53; k += 1)
    {
        const int written = snprintf(text + length, capacity - length, "    s += %s%03u;\n", prefixes[k % 3], (k * 389u + 17u) % 1000u);
        fits = written > 0 && (uint64_t)written < capacity - length;
        length += fits ? (uint64_t)written : 0;
    }
    const int tail = fits ? snprintf(text + length, capacity - length, "    return s;\n}\n") : -1;
    fits = tail > 0 && (uint64_t)tail < capacity - length;
    length += fits ? (uint64_t)tail : 0;
    return fits ? length : 0;
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
        // A pointer converted to int at file scope, each one's conversion and its "not a compile-time constant" (relayed
        // from the type attributes' session).
        "int x = \"str\";\nint y = \"two\";\nint z = 5;\nint w = \"three\";\n",
        // An array of unknown size whose initializer is no list: clang's words by its element's literal, recovered, the
        // array keeping its unknown size.
        "int y[] = 5;\nchar c[] = 6;\nstruct S { int x; };\nstruct S sa[] = 3;\nshort ss[] = 4;\nunsigned short us[] = 4;\n"
        "static int z[] = 2;\nvoid i(void) { int fine; }\nvoid h(void) { char loc[] = 1; int unused; }\nint a2[2] = 7;\n"
        "int g(void) { return y[0] + c[0]; }\n",
        // Its error after the initializer's own, none where the initializer names an undeclared identifier, and an extern
        // array's without -Wextern-initializer; a parenthesized initializer, a cast and a scalar's compound literal are no
        // list either (review on #1791).
        "int u1[] = undecl_fn2();\nint u2[] = undecl;\nint u3[] = undecl + 1;\nint u4[] = sizeof(undecl2);\nextern int e[] = 7;\n"
        "int h[] = (5);\nint i[] = (int)6;\nint j[] = (int){7};\nint n[] = 4, o[] = 5;\n",
        // None where the initializer is no valid value: a missing name outside a statement expression or in its value, a
        // missing member, a called non-function; one where a statement expression shields the name in an earlier
        // statement; and an array's compound literal only a prefix of the initializer (second review on #1791).
        "struct S { int a; };\nint f(void);\nint x;\nvoid g(void) { int u[] = ({ undecl; 5; }); }\n"
        "void h(void) { int u[] = ({ int y = undecl2; 5; }); }\nvoid k(void) { int u[] = ({ sizeof(undecl3); 5; }); }\n"
        "void n(void) { int u[] = ({ int z = ({ undecl8; 1; }); 5; }); }\nvoid q(void) { int u[] = ({ if (1) undecl10 = 2; 5; }); }\n"
        "void r(void) { int u[] = (undecl11); }\nint v1[] = ((struct S){0}).nosuch;\nint v4[] = (int[2]){1,2} + x;\nint v6[] = undecl6 + 1;\n",
        // A unit the reader rejects, whose retained graph ends at its first missing name: a later array's initializer has
        // no node and keeps its error (third review on #1791).
        "int c = undecl, d[] = 6;\nunknown_type z;\n",
        "int c = undecl;\nint d[] = 6;\nint e[] = (5);\nint f2[] = undecl2 + 1;\nint g[] = (int)7;\nunknown_type z;\n",
        // An enum redeclared with another fixed underlying type, without the one it had, or with one it hadn't: clang's
        // CheckEnumRedeclaration words and notes, after which the declaration, its body and its enumerators are dropped
        // and the name keeps the earlier enum (#1447). Typedef'd bases are spelled with their "aka", and an enum in a
        // record is read so too.
        "enum E : int;\n"
        "enum E : long;\n"
        "enum F : short { P };\n"
        "enum F : int;\n"
        "enum G { Q };\n"
        "enum G : int;\n"
        "enum H : int;\n"
        "enum H { R };\n"
        "int x = undeclared;\n",
        "typedef long L;\n"
        "typedef int I;\n"
        "enum E : I;\n"
        "enum E : L;\n"
        "enum F : I { F1 };\n"
        "enum F : int;\n"
        "enum F f;\n"
        "int s = sizeof(f);\n"
        "enum H : short;\n"
        "enum H { R = 1 };\n"
        "_Static_assert(sizeof(R) == 4, \"r\");\n"
        "_Static_assert(sizeof(enum H) == 2, \"h\");\n",
        "enum A : short { A1 };\n"
        "enum A : int { A2 };\n"
        "int a = A2;\n"
        "enum B { B1 };\n"
        "enum B : int { B2 };\n"
        "int b = B2;\n"
        "enum C : int;\n"
        "enum C : long { C1 };\n"
        "int c = C1;\n"
        "enum D : int;\n"
        "enum D;\n"
        "enum D : int { D1 };\n"
        "int d = D1;\n",
        "void h(void) { enum K : char { K1 }; enum K : int { K2 }; int k = K2; }\n"
        "void m(void) { enum N { N1 }; enum N : int; }\n",
        "struct T { enum Z : int { Z1 } a; enum Z : long { Z2 } b; int c; };\n"
        "int z = Z2;\n"
        "struct U { enum Y : short { Y1 } a; };\n"
        "enum Y : char;\n"
        "struct T t;\n"
        "int s = sizeof(t.c);\n",
        // The note is at the latest declaration clang kept, whose base the "(was …)" spells, not the first (review on
        // #1805); a dropped one's attributes go with it, unwarned.
        "enum E : short;\nenum E : short int;\nenum E : signed short;\nenum E : long;\n"
        "typedef int I;\nenum F : I;\nenum F : int;\nenum F : long;\n"
        "enum G : int;\nenum G;\nenum G : int;\nenum G g;\nenum G : long;\n"
        "void f(void) { enum K : char; enum K : char; enum K : int; }\n",
        "enum E : int { A };\nenum E : long { B } __attribute__((unused));\nenum __attribute__((unused)) E : long { C };\n"
        "enum F { F1 };\nenum F : int { F2 } __attribute__((unused));\nenum F;\nenum F : char;\n"
        "__attribute__((unused)) enum H : int { H1 };\n__attribute__((unused)) enum H : long { H2 };\n",
        // GNU's array initialized by a compound literal of its array type (#1447): the same element type and size or none
        // (the literal's size then the array's, `(int[]){…}`'s once its own initializer finished); another array type's
        // "cannot initialize array of type 'int[3]' with array of type 'int[2]'", recovered.
        "int a[] = (int[2]){1, 2};\n"
        "int b[2] = (int[2]){3, 4};\n"
        "int c[3] = (int[2]){5, 6};\n"
        "int d[1] = (int[2]){7, 8};\n"
        "char s[] = (char[]){\"hi\"};\n"
        "int e[] = ((int[3]){1});\n"
        "_Static_assert(sizeof(a) == 8 && sizeof(e) == 12, \"\");\n"
        "void f(void) { int x[] = (int[2]){1, 2}; int y[2] = (int[]){3, 4}; (void)x; (void)y; }\n",
        "int a1[] = (long[2]){1, 2};\n"
        "unsigned a2[] = (int[2]){1, 2};\n"
        "const int a3[] = (int[2]){1, 2};\n"
        "int a4[] = (const int[2]){1, 2};\n"
        "int a5[2][2] = (int[2][2]){{1, 2}, {3, 4}};\n"
        "int a6[] = (int[2]){1, 2, 3};\n"
        "char a7[] = (char[2]){\"abc\"};\n"
        "int a8[] = (int[2]){1} + 0;\n",
        // An enumerator whose name is another kind of the scope's (an object, a typedef, a parameter, a local): clang's
        // "redefinition of 'K'" with its note at the earlier declaration, the enumerator dropped and the next implicit
        // value restarting at 0, of int or of the fixed type (#1447).
        "int K;\n"
        "enum E { K, L };\n"
        "int use(void) { return K + L + undeclared; }\n",
        "enum E { A, B };\n"
        "int A;\n"
        "float B(void);\n"
        "typedef int A2;\n"
        "enum F { A2 };\n"
        "int use(void) { return A + B; }\n",
        "void f(int P)\n"
        "{\n"
        "    enum { P, Q } e;\n"
        "    int x = Q;\n"
        "    (void)e; (void)x;\n"
        "}\n",
        "int K;\n"
        "enum E { J = 5, K, M };\n"
        "_Static_assert(M == 0, \"m\");\n"
        "typedef long T;\n"
        "enum F : short { U = 7, T, V };\n"
        "_Static_assert(V == 0 && sizeof(V) == 2, \"v\");\n"
        "void w(void) { int loc; enum { loc, after } e; (void)e; (void)loc; }\n",
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
        // Recovered errors in several bodies, between the top level's, which the deferred reader's merge puts back in
        // source order's (#1447, lanes): attributes', a block's redeclarations naming types the bodies make, a typedef's,
        // statements outside their loops, and the bodies' warnings beside them.
        "int t1 __attribute__((aligned(3)));\n"
        "int f(void) { int a __attribute__((aligned(5))); int unused; return 0; }\n"
        "int t2 __attribute__((aligned(7)));\n"
        "int g(void) { int b __attribute__((aligned(9))); return 1; }\n"
        "int t3 __attribute__((aligned(11)));\n"
        "int h(int p) { if (p = 2) {} return p; }\n"
        "int k(void) { struct S { int x; } a; float a; a.x = 1; return a.x; }\n"
        "int m(void) { typedef struct Q { int y; } T; typedef long T; T q = { 2 }; return q.y; }\n"
        "int n(void) { break; int u; return 0; }\n"
        "int o(int p) { continue; return p << 40; }\n"
        "typedef int U;\nint q(void) { U x = 2; return x; }\nint U;\n",
        // An attribute expression's invalid operands, both of a type the body makes after an earlier body made its own:
        // the merge maps both operands' types, the second's held in the report's value (review on #1794).
        "int g(void) { struct G { int x; } gv; return gv.x; }\n"
        "int f(void)\n{\n    struct S { int a; } s;\n    int x __attribute__((annotate(\"n\", s + s))) = 0;\n    return x;\n}\n"
        "int h(void) { struct H { int y; } hv; return hv.y; }\n",
        // A repeated enumerator, dropped: the next one's implicit value restarts at 0 (of int, or of a fixed type), and
        // an enum left with none is unsigned int's.
        "enum A { X = 5 };\nenum B { Y = 3, X, Z };\n_Static_assert(Z == 0, \"z\");\n"
        "enum C : long { P };\nenum D : char { Q = 7, P, R };\n_Static_assert(R == 0 && sizeof(R) == 1, \"r\");\n"
        "enum E { M = -1 };\nenum F { M };\n_Static_assert(sizeof(enum F) == 4 && (enum F)-1 > 0, \"u\");\nint x = undeclared;\n",
        // Typo correction (#1447): a value's undeclared name recovers through the nearest visible name, as if it were
        // written (its type drives the later errors: a parameter's conversion, a case label's constant, a static
        // initializer's), unless another name is as near (alpha/alphb), the name is a typedef's, or it follows a cast;
        // an inner block's names are gone after it.
        "int counter, counter1, counter2;\nfloat ratio;\nint *pointer;\nenum { ALPHA, BETA };\ntypedef int myint;\n"
        "struct S { int a; } sval;\nvoid take(int *);\nstatic int s = countr;\nint alpha, alphb;\n"
        "int g(int param)\n{\n    int a = countr;\n    int b = counter + counte;\n    int c = ALPHAA;\n    int d = paramm;\n"
        "    int e = myin;\n    take(rati);\n    int *f = &ratoi;\n    int h = (int)countr;\n    svl.a = 1;\n    pointe = 0;\n"
        "    switch (param) { case countr: break; }\n    int i = alphc;\n    { int value; int j = valu; }\n    int k = value;\n"
        "    return a + b + c + d + e + h + i + k;\n}\n",
        // An undeclared function's call notes the function it may have meant; an implicit declaration is itself a name
        // of its block, which ties with the other (gnu17's: C23 has no implicit declaration).
        "/* gnu17 only */\nint compute(int);\nint counter;\nvoid f(void) { int x = compte(1); }\nvoid g(void) { int d = countr; }\n"
        "void k(void) { { countre(1); } int e = countr; }\nvoid m(void) { countre(1); int e = countr; }\n",
        // Three thousand names and fifty-three typos, written out where the fixture is (check_test_typo_stress): each
        // search reads the visible names once (c_check_typo.c).
        "/* typo stress */\n",
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
        "void f(void) { struct L *p; struct L { int a; }; struct L { int b; }; (void)p; }\n"
        // A fixed enum listed again with its base, at file and block scope.
        "enum FE : int { FA, FB };\nenum FE : int { FC, FD };\nint fx = FC + FB;\nvoid fg(void) { enum FL : char { FL1 }; enum FL : char { FL2 }; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    // The tag redefinitions, the last three fixtures, are gnu17's alone: C23 allows a compatible one and words an
    // incompatible one otherwise (N3037), which the checker doesn't model yet.
    const uint32_t before_c23 = BUSTER_ARRAY_LENGTH(fixtures) - 3;
    static const char* const options[] = { NULL, "-Wall", "-ferror-limit=2", "-w" };
    static char stress_text[65536];
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/semantic-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/semantic-recovery-%u.c", directory, f);
        const bool stress = strncmp(fixtures[f], "/* typo stress */", 17) == 0;
        const uint64_t stress_length = stress ? check_test_typo_stress(stress_text, sizeof(stress_text)) : 0;
        const bool made = stress ? stress_length != 0 && parse_test_write(source, stress_text, stress_length) :
                                   parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        // A fixture marked as gnu17's alone, which C23's rules or a wording outside this suite reads otherwise.
        const bool c17_only = strncmp(fixtures[f], "/* gnu17 only */", 16) == 0;
        const uint32_t standard_count = f >= before_c23 || c17_only ? 1u : (uint32_t)BUSTER_ARRAY_LENGTH(standards);
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
                // The deferred readings' arguments: -c's, of no object (-fsyntax-only starts no lanes).
                char* compile[16] = { (char*)executable, (char*)standards[s], "-c", "-o", object, "-fno-caret-diagnostics", source };
                uint32_t compile_count = 7;
                if (options[o] != NULL)
                {
                    compile[compile_count++] = (char*)options[o];
                }
                check_test_semantic_recovery_lookahead(expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "semantic recovery: clang rejects the fixture and its lines are read");
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
                    check_test_semantic_recovery_lookahead(actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "semantic recovery %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f, standards[s],
                             options[o] != NULL ? options[o] : "", mode, status, oracle, actual_lines, expected_lines);
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
