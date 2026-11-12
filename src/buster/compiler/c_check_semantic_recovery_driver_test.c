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
        // A block's thread-local object without static or extern (#2420): clang's "'_Thread_local' variables must have global
        // storage" at the keyword ('__thread' for GNU's, '_Thread_local' for C23's thread_local), and the reading goes on.
        "void c(void) { _Thread_local int z; __thread int w = 1; (void)z; (void)w; int u = undeclared; }\n"
        "void d(void) { static _Thread_local int s; extern _Thread_local int e; (void)s; (void)e; }\n"
        "void k(int p) { for (_Thread_local int i = 0; i < 1; i++) {} }\n"
        "void m(void) { _Thread_local int q __asm__(\"q\"); thread_local int t; }\n",
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
        // A TLS-only mismatch keeps the declaration in the chain (#1786): the next declaration is compared with it, its
        // storage its own, and a type conflict notes it. Both directions, tentative and extern declarations.
        "_Thread_local int kept_tls;\nint kept_tls;\nint kept_tls;\n"
        "_Thread_local int kept_type;\nint kept_type;\nfloat kept_type;\n"
        "int kept_plain;\n_Thread_local int kept_plain;\nfloat kept_plain;\n"
        "extern _Thread_local int kept_extern;\nextern int kept_extern;\nextern float kept_extern;\n"
        "extern int kept_back;\nextern _Thread_local int kept_back;\nextern _Thread_local int kept_back;\nextern int kept_back;\n"
        "int use_kept(void) { return kept_tls + kept_type + kept_plain + kept_extern + kept_back; }\n",
        // Its redefinition's error follows the TLS one, noted at the first definition; a later declaration of the chain
        // sees the kept one, and a definition after it is the redefinition alone.
        "_Thread_local int defined_tls = 1;\nint defined_tls = 2;\n"
        "int defined_plain = 1;\n_Thread_local int defined_plain = 2;\n"
        "_Thread_local int defined_later = 1;\nint defined_later;\nint defined_later;\nint defined_later = 3;\n"
        "int defined_extern = 1;\nextern _Thread_local int defined_extern;\nextern _Thread_local int defined_extern;\n",
        // Block externs: within one block, across blocks, and a file-scope declaration after another block's.
        "void block_chain(void) { extern _Thread_local int chained; extern int chained; extern float chained; }\n"
        "void block_first(void) { extern _Thread_local int across; }\n"
        "void block_second(void) { extern int across; extern float across; }\n"
        "void block_file(void) { extern _Thread_local int filed; }\nint filed;\nint filed;\nfloat filed;\n"
        "int use_filed(void) { return filed; }\n",
        // An object's redefinition stays in its chain, invalid (#1868): a later declaration's type isn't checked against
        // it, while linkage and thread-local errors note it, and a third definition notes the first.
        "int a = 1;\nint a = 2;\nfloat a;\n"
        "int b = 1;\nint b = 2;\nstatic int b;\n"
        "int c = 1;\nint c = 2;\nextern float c;\n"
        "int d = 1;\nint d = 2;\nint d = 3;\n"
        "int z = 1;\nint z = 2;\n_Thread_local int z;\n"
        "_Thread_local int y = 1;\nint y = 2;\nint y;\n"
        "void blk(void) { int l; int l; float l; (void)l; }\n"
        "void blk2(void) { extern int m; }\nint m = 1;\nint m = 2;\nlong m;\n"
        "int use(void) { return b + d + y + m; }\n",
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
        // A unit with a parse error, checked as the prefix of complete declarations before it, whose reader recovers from
        // a missing name as a whole unit's does (#1799, Parser.prefix_view): the graph's errors past the name are clang's,
        // and an unsized array's list error gives way to its initializer's own.
        "int c = undecl_n, x = 1, y = x();\nunknown_type z;\n",
        "int c = undecl_n;\nint y = 3 + (void)0;\nunknown_type z;\n",
        "int c = undecl_n, d[] = 1 + (void)0;\nunknown_type z;\n",
        "int c = undecl_n;\nint f(int);\nint y = f(1, 2);\nunknown_type z;\n",
        // Clang's words and places (#1447): a static initializer's culprit (an operator, a member's name, a cast's `(`, a
        // call's or a name's first token), `sizeof` and `alignof` of an incomplete type (the type as written, at a type
        // name's keyword or an expression's start, with the tag's note; an array of unknown size's alignment its
        // element's), and a call of no function (the callee's type, at the `(`).
        "int k;\n"
        "static int s2 = k + 1;\n"
        "static int s3 = 1 + k;\n"
        "static int s4 = (k);\n"
        "static int s5 = -k;\n"
        "static int s6 = k * 2 + 3;\n"
        "static int s7 = 2 + 3 * k;\n"
        "static int *p1 = &k + k;\n",
        "int k;\n"
        "int f(void);\n"
        "struct S { int a; } s;\n"
        "int arr[4];\n"
        "static long c1 = (long)k;\n"
        "static int c2 = f();\n"
        "static int c3 = k ? 1 : 2;\n"
        "static int c4 = s.a;\n"
        "static int c5 = arr[k];\n"
        "static int c6 = ((k));\n"
        "static int c7[2] = { 1, k };\n"
        "static struct S c8 = { k };\n"
        "static int c9 = (int)(k + 1);\n"
        "static int c10 = k++;\n"
        "static int *c11 = &arr[k];\n"
        "static int c13 = !k;\n",
        // A value converted to another type: the conversion is clang's culprit, at the converted expression's first token,
        // its parentheses included; a callee's type is the loaded one, its qualifiers and _Atomic dropped (review on #1802).
        "int k;\nstruct S { int a; } s;\nenum E { X };\n"
        "static long v1 = k + 1;\nstatic long v2 = s.a;\nstatic int v3 = sizeof(k) + k;\nstatic long v4 = (k);\n"
        "static long v5 = (int)k;\nstatic long v6[2] = { k + 1, 2 };\nstatic float v7 = k * 2;\nstatic unsigned v8 = -k;\n"
        "static void *v9 = (char *)0 + k;\nstatic const int v10 = k + 1;\nstatic enum E v11 = k + 1;\n"
        "static struct { long x : 3; int y; } v12 = { k + 1, k + 2 };\nstatic _Bool v13 = k + 1;\n",
        "const int cx; volatile int vx; _Atomic int ax; typedef const int CT; CT ctx; typedef int T; T tx; const T ctt;\n"
        "typedef _Atomic int AT; AT atx; int *const pc; struct Q { int a; } const cq;\n"
        "void f(void) { cx(); vx(); ax(); ctx(); tx(); ctt(); atx(); pc(); cq(); }\n",
        // An array's or function's qualifiers are its elements', which the loaded type keeps (review on #1802).
        "struct Q { int a; };\n"
        "const int ca[2]; volatile int va[2]; const int na[2][3]; int * const pa[2]; const _Atomic int aa[2]; const struct Q cq2[2];\n"
        "int (* const fpa[2])(void);\n"
        "void g(void) { ca(); va(); na(); pa(); aa(); cq2(); fpa(); }\n",
        "extern int a[];\n"
        "struct Q;\n"
        "int s = sizeof(a);\n"
        "int t = sizeof(struct Q);\n"
        "int u = sizeof a;\n"
        "int v = _Alignof(struct Q);\n"
        "void f(void);\n"
        "int w = sizeof(f());\n"
        "int x = sizeof(void);\n",
        "struct Q;\n"
        "int a1 = __alignof__(struct Q);\n"
        "int a2 = __alignof(struct Q);\n"
        "int a4 = _Alignof(struct Q);\n"
        "extern int e[];\n"
        "int a5 = __alignof__(e);\n"
        "typedef struct Q QT;\n"
        "int a6 = sizeof(QT);\n"
        "union U; enum E;\n"
        "int a7 = sizeof(union U) + sizeof(enum E);\n"
        "int a8 = sizeof(struct Q *) + sizeof(e[0]);\n"
        "void g(void) { struct Q *q = 0; int a9 = sizeof(*q); int a10 = sizeof *q; }\n",
        "extern int e[];\n"
        "_Static_assert(__alignof__(e) == 4, \"\");\n"
        "_Static_assert(_Alignof(int[]) == 4, \"\");\n"
        "_Static_assert(_Alignof(char[][4]) == 1, \"\");\n"
        "extern double d2[][3];\n"
        "_Static_assert(__alignof__(d2) == 8, \"\");\n"
        "struct Q;\n"
        "int q = _Alignof(struct Q);\n",
        "int f(void);\n"
        "int x;\n"
        "struct S { int a; } s;\n"
        "void g(void) { f()(); x(); s(); (1)(); int *p = 0; p(); }\n",
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
        // A fixed enum's enumerator whose value isn't representable (#1807): clang's error, recovered with the value
        // converted or wrapped, and a name clash at the same enumerator reported after it, with its note.
        "int K;\n"
        "enum E : char { K = 200, L };\n"
        "_Static_assert(L == 0, \"l\");\n"
        "enum A : unsigned char { A1 = 255, A2 };\n"
        "enum B : signed char { B1 = 127, B2, B3 };\n"
        "_Static_assert(B2 == -128 && B3 == -127, \"b\");\n"
        "enum __attribute__((mode(QI))) C { C1 = 300, C2 };\n"
        "enum D : char { M = 200, N, P, P = 400 };\n"
        "_Static_assert(N == -55, \"n\");\n"
        "int use(void) { return A2 + B3 + C2 + undeclared; }\n",
        // A typedef's and a parameter's declarator errors (#1447): a typedef keeps its stand-in type, whose uses are
        // checked; a parameter is an invalid stand-in int, quiet in its body, and makes the function whose own list it is
        // invalid, but not a function pointer. After a declarator whose own type is in error, the group's later
        // declarators aren't declared, and a typedef name it dropped is "unknown type name", which still stops.
        "typedef int U[-1];\n"
        "U u1 = \"x\";\n"
        "int w = u1 + (char*)0;\n"
        "void k(void) { U loc; int *pp = loc; typedef int L[-3]; L l = \"s\"; (void)pp; (void)l; }\n"
        "typedef struct X SX[3];\n"
        "SX sx;\n"
        "typedef int F(void)[3];\n"
        "int bad(int q[-1]);\n"
        "int call = bad(\"x\");\n"
        "void (*fp)(int r[-1]);\n"
        "void use(void) { char *c = bad(1); fp(\"y\"); (void)c; }\n"
        "int def(int a[-1], int b) { int *pk = a; char *c = b; (void)pk; (void)c; return 0; }\n"
        "int c2 = def(\"z\", 1);\n"
        "int nested(void (*cb)(int n[-1]), int m[-2]);\n"
        "int g(int q2[-1]), h(int);\n"
        "int z = h(\"w\");\n"
        "int a[-1], b;\n"
        "int c = b;\n"
        "void f(void) { int p[-1], r; int s = r; (void)s; }\n"
        "int x = undeclared;\n",
        // An array bound of another type than an integer's (#2170): clang's "size of array has non-integer type", its
        // declaration invalid and its uses quiet, in a function, at file scope, in a typedef, a variable array's, a
        // conditional's, a cast's.
        "void h(int n) { int k[(long double)1]; int *pk = k; (void)pk; }\n"
        "int g1[1.5];\n"
        "int use1 = g1 + 1;\n"
        "void h2(void) { int k2[2.0f]; double d = 1; int k3[d]; (void)k2; (void)k3; }\n"
        "typedef int T[(double)2];\n"
        "T t;\n"
        "void h3(void) { int k4[(void*)0 == 0 ? 1.0 : 2]; (void)k4; }\n"
        "int after = undeclared;\n",
        "typedef int T, U[-1], V;\nV v;\n",
        // Initialized invalid declarations (#1447): an object's initializer is skipped, the object invalid and its uses
        // quiet; a function's is clang's "illegal initializer", after the declarator's own error.
        "int a[-1] = {1, \"x\"};\n"
        "int c = a[0];\n"
        "struct X; struct X xs[2] = {0};\n"
        "void v = 1;\n"
        "int (fa[2])(void) = {0};\n"
        "int big[1ull<<62] = {0};\n"
        "void f(void) { int l[-1] = {2}; char *p = l; int m = l[0]; (void)p; (void)m; }\n"
        "int g(void)[3] = 0;\n"
        "int h(void)[3] = {1}, k;\n"
        "void f2(void) { void v1 = 1, w1; int u = q; }\n"
        "int x = undeclared;\n",
        // An array of an element whose alignment an attribute raised past its size (#1893): clang's "size of array element
        // of type 'enum E' (4 bytes) isn't a multiple of its alignment (8 bytes)" at the `[`, recovered, the declaration
        // invalid and its uses quiet; arrays of pointers to it and an over-aligned record whose size grew are fine.
        "enum __attribute__((aligned(8))) E { A };\n"
        "enum E arr[3];\n"
        "enum E two[2][2];\n"
        "void f(void) { enum E loc[2]; int u = loc[0] + q; (void)u; static enum E s[4]; }\n"
        "enum E *ptrs[3];\n"
        "struct A8 { char c; } __attribute__((aligned(8)));\n"
        "struct A8 ok[3];\n"
        "int use = arr[0] + two[0][0] + sizeof(arr) + sizeof(ptrs) + sizeof(ok);\n"
        "int x = undeclared;\n",
        // (A typedef's and a parameter's, which recover as their other declarator errors do.)
        "enum __attribute__((aligned(8))) E { A };\ntypedef enum E TE;\ntypedef enum E TA[2];\nTA ta;\n"
        "int g(TE p[3]) { return p[0]; }\nint h(enum E q[2]);\nint c = h(0) + g(0);\nint x = undeclared;\n",
        // C23's enum redefinitions in a block, and a third and fourth definition (#1826): compatible ones are taken, and an
        // ambiguous use lists every candidate, a block's qualified by its function (`g(int, char *)::B`, `k()::P`).
        "/* gnu23 only */\n"
        "enum X { A, B };\nenum X { A, C };\nenum X { A, D };\nenum X { A, E };\nint u = A;\n"
        "void f(void) { enum Z { Z1 }; enum Z { Z1 }; int w = Z1; (void)w; }\n"
        "void g(int x, char *y) { enum Y { B, C }; enum Y { B, D }; int v = B; (void)x; (void)y; (void)v; }\n"
        "int k(void) { enum W { P, Q }; enum W { P, R }; enum W { P, S }; return P; }\n"
        "int z = undeclared;\n",
        // An unnamed member record that is a struct in one definition and a union in the other (#1826): clang's error is
        // the inner record's, "'' is a struct here" at the earlier one, without the outer member's notes; nested too.
        "/* gnu23 only */\nstruct S { struct { int a; } *p; };\nstruct S { union { int a; } *p; };\n"
        "struct U { union { int a; } *p; int z; };\nstruct U { struct { int a; } *p; int z; };\n"
        "struct V { int k; struct { union { int a; } *q; } *p; };\nstruct V { int k; struct { struct { int a; } *q; } *p; };\nint use = undeclared;\n",
        // A member record that is an incompatible redefinition itself, behind _Atomic, const, pointers and arrays (#1826):
        // the outer error's note spells it an int in its place (`_Atomic(int) *`), and the inner record is found past the
        // qualifier that heads its subtree.
        "/* gnu23 only */\nstruct S { _Atomic struct T { int a; } *p; };\nstruct S { _Atomic struct T { long a; } *p; };\n"
        "struct W { const struct X { int a; } * volatile p[3]; };\nstruct W { const struct X { long a; } * volatile p[3]; };\n"
        "struct Y { _Atomic struct { int a; } *p; };\nstruct Y { _Atomic union { int a; } *p; };\n",
        // C23's redefinition of a record in its scope (#1447, N3037), gnu23's alone: a compatible one redeclares the
        // earlier record, typedef'd members and nested anonymous records too; an incompatible one is clang's "has
        // incompatible definitions" with its notes at the first difference (a name, a bit-field, a type, a member more or
        // fewer), and attributes one definition hasn't are -Wodr's error.
        "/* gnu23 only */\n"
        "struct S { int a; long b; };\n"
        "struct S { int a; long b; };\n"
        "union U { int x; float y; };\n"
        "union U { int x; float y; };\n"
        "struct N { struct S s; int arr[3]; int bits : 3; };\n"
        "struct N { struct S s; int arr[3]; int bits : 3; };\n"
        "int use(void) { struct S v = { 1, 2 }; struct N n; n.bits = 1; return v.a + n.arr[0]; }\n"
        "void blk(void) { struct L { int q; }; struct L { int q; } l; l.q = 0; }\n"
        "int bad = undeclared;\n",
        "/* gnu23 only */\n"
        "struct S { int a; long b; };\n"
        "struct S { int a; int b; };\n"
        "struct T { int a; };\n"
        "struct T { int b; };\n"
        "struct U { int a; int b; };\n"
        "struct U { int a; };\n"
        "struct W { int a : 3; };\n"
        "struct W { int a : 4; };\n"
        "struct X { int a; } __attribute__((packed));\n"
        "struct X { int a; };\n"
        "struct Y { int a; };\n"
        "struct Y { const int a; };\n"
        "int x = undeclared;\n",
        "/* gnu23 only */\n"
        "struct A { int a; int b : 3; };\n"
        "struct A { int a; int b; };\n"
        "struct B { int a; };\n"
        "struct B { int a; int b; };\n"
        "struct C { struct { int x; } in; };\n"
        "struct C { struct { int x; } in; };\n"
        "struct D { int a; };\n"
        "struct D { int a; } __attribute__((aligned(8)));\n"
        "struct E { int a[2]; };\n"
        "struct E { int a[3]; };\n"
        "typedef int I;\n"
        "struct F { I a; };\n"
        "struct F { int a; };\n"
        "struct G { int a, b; };\n"
        "struct G { int b, a; };\n"
        "union H { int a; };\n"
        "union H { int a; long b; };\n"
        "void f(void) { struct L { int q; }; struct L { int r; }; }\n",
        "/* gnu23 only */\n"
        "struct D { int a; };\n"
        "struct D { int a; } __attribute__((aligned(8)));\n"
        "struct X { int a; } __attribute__((packed));\n"
        "struct X { int a; } __attribute__((packed));\n"
        "int x = undeclared;\n",
        // A C23 parameter list's redefinition of its own record (#1869) is compared as any scope's: incompatible ones in a
        // prototype, a block's prototype and a union, a compatible one declaring the first's type, and a nested
        // prototype's definition, which is a new scope's and no redefinition.
        "/* gnu23 only */\n"
        "int f(struct S { int x; } a, struct S { int y; } b);\n"
        "int g(struct T { int x; } a, struct T { int x; } b, struct T *c) { return a.x + b.x + c->x; }\n"
        "int h(union U { int i; } a, union U { int i; float f; } b);\n"
        "void k(void) { int m(struct V { int a; } *p, struct V { long a; } *q); }\n"
        "int n(struct W { int a; } *p, int (*q)(struct W { int a; } *r));\n"
        "int bad = undeclared;\n",
        // Clang's C23 comparison of members' types (review on #1813): any two named records are compatible, a typedef's
        // unnamed one too, and the unnamed records written in the members are compared member by member, through
        // pointers, arrays, atomics and functions, after the definition's own members.
        "/* gnu23 only */\n"
        "struct I { int a; }; struct J { long b; }; union U { int a; };\n"
        "typedef struct { int a; } AX; typedef union { int a; } UY;\n"
        "struct S { struct I m; struct I *p; struct I a[2]; struct I (*f)(void); AX t; struct { int x; } *q; };\n"
        "struct S { struct J m; union U *p; union U a[2]; union U (*f)(void); UY t; struct { int x; } *q; };\n"
        "struct T { struct { int y; } arr[2]; const struct { int z; } *cp; _Atomic struct { int v; } *ap; struct { int u; } (*g)(int); struct { int w; }; };\n"
        "struct T { struct { int y; } arr[2]; const struct { int z; } *cp; _Atomic struct { int v; } *ap; struct { int u; } (*g)(int); struct { int w; }; };\n"
        "struct T t; int x = t.w + undeclared;\n",
        // Their differences: at the innermost unnamed record (its enclosing records' names before its place), an unnamed
        // bit-field's notes, and an incompatible inner record's member read as an int.
        "/* gnu23 only */\n"
        "struct A { struct { int x; } *p; };\nstruct A { struct { long x; } *p; };\n"
        "struct B { struct { struct { int y; } in; } m; };\nstruct B { struct { struct { char y; } in; } m; };\n"
        "struct C { struct { int x; }; int y; };\nstruct C { struct { long x; }; int y; };\n"
        "struct D { int :3; };\nstruct D { int :4; };\n"
        "struct E { struct I { int a; } m; };\nstruct E { struct I { long a; } m; };\n"
        "struct F { int a; struct { int x; } *p; };\nstruct F { long a; struct { long x; } *p; };\n",
        // Attributes, arguments included: the record's own, a member's (each clang's -Wodr error, the comparison going on)
        // and an unnamed member record's, each noted where the lists first differ.
        "/* gnu23 only */\n"
        "struct A { int a; } __attribute__((visibility(\"hidden\")));\nstruct A { int a; } __attribute__((visibility(\"protected\")));\n"
        "struct B { int a; } __attribute__((packed, aligned(4)));\nstruct B { int a; } __attribute__((aligned(4), packed));\n"
        "struct C { int a; } __attribute__((aligned(4)));\nstruct C { int a; } __attribute__((aligned(8)));\n"
        "struct D { int a __attribute__((aligned(8))); int b __attribute__((deprecated(\"x\"))); };\n"
        "struct D { int a; int b __attribute__((deprecated(\"y\"))); };\n"
        "struct E { int a; } __attribute__((packed));\nstruct E { long a; };\n"
        "struct F { int a __attribute__((aligned(8))); int b; };\nstruct F { int a; long b; };\n"
        "struct G { struct { int a; } __attribute__((packed)) m; };\nstruct G { struct { int a; } m; };\n",
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
        // Specifiers that don't combine, which clang's DeclSpec drops, the declaration staying valid (#1447): a second type,
        // width or sign is dropped at its error; a sign or a width the type doesn't take drops the sign, or the type
        // (`long float` is a long). The declarations' types show in later conversions, and the bodies' warnings follow.
        "long long long a1;\nint float a2;\nfloat int a3;\nunsigned float a4;\nlong float a5;\nshort double a6;\nlong _Bool a7;\n"
        "signed unsigned a8;\nchar int a9;\nlong long double a10;\ndouble float a11;\n"
        "int use(void) { char *p = &a1; p = &a2; p = &a3; p = &a4; p = &a5; p = &a6; p = &a7; p = &a8; p = &a10; p = &a11; return a9; }\n"
        "static int g(int a) { int unused; if (a = 1) return 2; return 3; }\n"
        "int h(void) { int z; return z; }\n",
        // The same in a body, a parameter, a cast, sizeof's type name and a member, and a repeated specifier's warning.
        "int f(long long long p, int float q) { short long z = 1; return (int)(unsigned signed)p + q + z; }\n"
        "int g(void) { int unused; char int c = 1; if (c = 2) return 1; return sizeof(long float); }\n"
        "struct T { long long long m; int float n; } t;\nlong double long ld;\n_Complex _Complex double cd;\n"
        "static int h(void) { int u; return 0; }\n",
        // A _Complex the type doesn't take, dropped at the list's end, before and after the type, and the declarations after
        // it checked; a _BitInt named as clang names it in a combination's error (review on #1820).
        "_Complex _Bool cb;\nint float y;\n_Bool _Complex cb2;\nint float z;\n_BitInt(8) int b;\n",
        // A _Complex after a type of no keyword, and in the operand readers' type names: a typeof's, an _Atomic's and an
        // enum's base (re-review on #1820), each dropped as clang drops it, the unit checked after it.
        "typeof(_Bool) _Complex x1;\nint a1 = undeclI;\n_BitInt(8) _Complex x2;\n_Atomic(int) _Complex x3;\nstruct SI;\nstruct SI _Complex x4;\n"
        "enum EI;\nenum EI _Complex x5;\ntypeof(_Complex _Bool) x6;\n_Atomic(_Complex _Bool) x7;\nenum EU : _Complex _Bool { EA9 };\n"
        "_Static_assert(sizeof(x6) == 1 && sizeof(enum EU) == 1, \"b\");\nint a2 = undeclJ;\n",
        // A repeated _Complex, whose warning clang gives before the list's end's error; every _Complex is dropped.
        "_Complex _Complex _Bool y1;\nint float y2;\n_Complex _Bool _Complex y3;\nint float y4;\n_BitInt(8) _Complex z;\n"
        "int plain = undeclared_var;\nstruct S;\n_Complex _Complex struct S y5;\nint float y6;\nenum E5 : _Complex _Bool { A5 };\n",
        // The operand readers in a body, a cast, sizeof, a parameter and a member, and a list's end's errors after its
        // repeated specifiers' warnings.
        "int f(typeof(_Complex _Bool) p, _Atomic(int float) q) { typeof(_Complex _Bool) a = 1; _Atomic(long float) b = 2;\n"
        "  return (int)(typeof(int float))a + sizeof(typeof(unsigned float)) + p + q + b; }\n"
        "struct T { typeof(long float) m; _Atomic(_Complex _Bool) n; } t;\nenum E1 : long float { A1 };\nenum E3 : int char { A3 };\n"
        "const _Complex const _Bool c1;\nvolatile long volatile float c2;\n_Complex long _Bool c3;\n"
        "typeof(typeof(_Complex _Bool) _Complex) c4;\nstatic int g(void) { int u; return 0; }\n",
        // Several errors in one list (#1820 (c)): each specifier whose slot is taken is an error at it and dropped, the
        // list going on, then its end's in DeclSpec::Finish's order (a sign, then a width, then _Complex), each recovered.
        "int float char x;\nint a1 = u1;\n_Complex _Bool int y;\nint a2 = u2;\n_Complex _Bool _Atomic _Bool y2;\nint a3 = u3;\n"
        "long long long long w;\nint a4 = u4;\nsigned unsigned float v;\nint a5 = u5;\nunsigned long float double d;\nint a6 = u6;\n"
        "unsigned unsigned float uu;\nint a7 = u7;\nlong double long double ldd;\nint a8 = u8;\n",
        // The same in the operand readers' type names, a parameter, a body, a cast, sizeof, _Alignof and members.
        "typeof(unsigned unsigned float) q1;\nint b1 = v1;\ntypeof(int float char) q2;\nint b2 = v2;\n"
        "enum F : long long long long { B };\nint b4 = v4;\n_Atomic(unsigned long float double) q3;\nint b5 = v5;\n"
        "int f(int float char p, long long long long q) { short long char z = 1;\n"
        "  return (int)(unsigned signed float)p + q + z + sizeof(signed unsigned double); }\n"
        "struct T { long long long long m; int float char n; _Complex _Bool int k; } t;\nint b6 = v6;\n"
        "int g(void) { return _Alignof(_Complex _Bool int) + sizeof(typeof(unsigned float double)); }\nint b9 = v9;\n",
        // A second type of no keyword, dropped with its group (a _BitInt's, an _Atomic's, a tag's, a typeof's), a sign
        // repeated after the type, and a repeated _Complex dropped beside a conflict.
        "int _BitInt(8) x;\nint a1 = u1;\nint struct S2 { int m; } w2;\nstruct S2 s2;\nint a3 = u3;\n"
        "int typeof(int) _Atomic(int) unsigned unsigned v;\nint a4 = u4;\n_Complex _Complex int _Bool cb;\nint a5 = u5;\n"
        "unsigned float unsigned y;\nint a6 = u6;\nint _Atomic(int) z;\nint a7 = u7;\n"
        "void f(void) { int float char x; char *p = &x; int typeof(long) y; p = &y; }\n",
        // A block's object with linkage beside one without (#2441): clang's extern, non-extern, static and non-static
        // wordings and notes, then the unit goes on.
        "void a(void) { int q; extern int q; }\n"
        "void b(void) { static int r; extern int r; }\n"
        "void c(void) { extern int s; static int s; }\n"
        "void d(void) { extern int t; int t; }\n"
        "void e(void) { extern int u; extern float u; }\n"
        "void f(void) { extern int v; float v; }\n"
        "void g(void) { extern int q; int q = 2; }\n"
        "void h(void) { extern int r; static int r; }\n"
        "int s;\n"
        "void k(void) { extern int s; int s; (void)s; }\n"
        "int z = undeclared;\n",
        // A definition that is also an alias or ifunc (#2449): clang's handler drops the attribute, and the declaration stays a
        // valid definition, its later uses quiet.
        "int x __attribute__((alias(\"y\")));\n"
        "int y;\n"
        "int use(void) { return x + y; }\n"
        "int z = undeclared;\n"
        "void *r(void) { return 0; }\n"
        "int h(void) __attribute__((ifunc(\"r\"))) { return 3; }\n"
        "int k(void) { return h() + x + nope; }\n"
        "extern int w __attribute__((alias(\"y\"))) = 4;\n"
        "int m(void) { return w + nope2; }\n"
        "static int s __attribute__((alias(\"y\")));\n"
        "int n(void) { return s; }\n",
        // A recovered error in a definition's head, a specifier's or an attribute's, and then errors that stop the reader
        // (#1835): the later bodies keep their warnings, and the definitions' own bodies theirs, since clang gives those
        // errors before the body opens its scope; a body with an error of its own loses its unused variables.
        "int f(long float q) { int v; return q; }\n__attribute__((section(1))) int g(void) { int w; return 0; }\n"
        "int h(__attribute__((aligned(3))) int q) { int x; return q; }\nint p(__attribute__((aligned(3))) int q);\n"
        "void k(long float r) { int y = undeclJ; }\nint a = undeclI;\nstatic int s(void) { int u; return 0; }\nint m(void) { int z; return z; }\n",
        // A repeated specifier in an operand reader's type name (#1836): a typeof's, an _Atomic's, sizeof's, a cast's and
        // an enum's base, warned as clang's DeclSpec warns, a dropped _Complex's too.
        "typeof(unsigned unsigned int) q;\nint r = sizeof(typeof(const const int));\nenum E : unsigned unsigned short { A };\n"
        "enum F : _Complex _Complex _Bool { B };\nenum G : short short { C };\n_Atomic(signed signed char) s;\ntypeof(_Atomic _Atomic int) t;\n"
        "int f(void) { return sizeof(short short) + (int)(typeof(volatile volatile long))1 + _Alignof(_Complex _Complex double); }\n"
        "typeof(typeof(signed int signed) const const) u;\n",
        // A typeof specifier's sugar in a warning's type (#1837), as clang prints it: a type name's, a typedef name's, a nested
        // typeof's and one name's expression's, qualified, a sibling declarator's, a parameter's and a body's local.
        "typedef long L;\n"
        "struct S { int a; };\n"
        "int x;\n"
        "typeof(long) m1; char *p1 = &m1;\n"
        "__typeof__(unsigned) m2; char *p2 = &m2;\n"
        "typeof(L) m4; char *p4 = &m4;\n"
        "typeof(int *) m5; char *p5 = &m5;\n"
        "typeof(x) m6; char *p6 = &m6;\n"
        "const typeof(long) m7; char *p7 = &m7;\n"
        "typeof(struct S) m8; char *p8 = &m8;\n"
        "typeof(typeof(long)) m9; char *p9 = &m9;\n"
        "typeof(_Bool) m10; char *p10 = &m10;\n"
        "typeof(long) a11, *b11; char *p11 = &a11;\n"
        "int f(typeof(short) q) { char *p = &q; return *p; }\n"
        "typedef typeof(long) TL; TL m13; char *p13 = &m13;\n"
        "int g(void) { typeof(unsigned char) c = 1; int *q = &c; return *q; }\n",
        // An extern declaration of type void, which clang takes (#1447): its address, sizeof, a discarded use and a cast to
        // void are quiet, a block's extern too; reading its value is the error. A dereference's or subscript's of a void
        // pointer still warns, and a void value in a comma's, conditional's or selection's operand is quiet.
        "extern void v;\nextern const void cv;\nvoid *p = &v;\nconst void *q = &cv;\nint s = sizeof v;\nint x, c;\n"
        "void g(void) { v; (void)v; int unused; }\nvoid h(void) { extern void lv; void *r = &lv; }\n"
        "void warns(void) {\n    *p;\n    q[0];\n    (1, v);\n    (v, x);\n    c ? v : cv;\n    c ? *p : *p;\n"
        "    _Generic(x, int: v, default: cv);\n    _Generic(x, int: x, default: v);\n}\nint f(void) { return v; }\n",
        // File-scope declarations whose error no smaller part holds (#1447, check_driver_rejected_declaration): the view leaves
        // the declaration out, and the bodies after it give their warnings; a const object's initializer, specifiers that
        // don't combine, and two such declarations in a row.
        "const int c = undeclared_name;\nstatic int g(int a) { int unused; if (a = 1) return 2; return 3; }\nint h(void) { int z; return z; }\n",
        "long long long x;\nint m(int a) { int unused; if (a = 2) return 1; return 0; }\n",
        "const int c = nope;\nint float y;\nint k(int a) { int unused; if (a = 3) return 1; return 0; }\n",
        // A dropped part's error counts just before the token after it (#1447): a block's dropped initializer and a dropped
        // statement right after its body's `{` stay in their scope, whose unused variables are left out, as clang leaves
        // them, and the next body keeps its own.
        "struct S { int a; } s;\nvoid f(void) { int y = s.b; int z; }\nvoid g(void) { s.b; int w; }\nvoid h(void) { int unused; }\n",
        // A body that names the left-out declaration is left out with it, and one that doesn't keeps its warnings.
        "const int c = nope;\nint uses(void) { return c; }\nint m(int a) { int unused; if (a = 2) return 1; return 0; }\n",
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
        // A statement's first name before `=` takes an object alone (clang's StatementFilterCCC), where one within an
        // expression takes an enumerator too; a typedef is no candidate within an expression (review on #1822).
        "enum { COUNTER };\ntypedef int counting;\nint counter;\n"
        "void f(int n)\n{\n    COUNTR = 5;\n    if (n) COUNTR = 1;\n    lbl: COUNTR = 2;\n    switch (n) { case 1: COUNTR = 3; }\n"
        "    n = n ? COUNTR : 1;\n    int x = countin;\n    countin = 1;\n    (void)x;\n}\n",
        // An undeclared function's call notes the function it may have meant; an implicit declaration is itself a name
        // of its block, which ties with the other (gnu17's: C23 has no implicit declaration).
        "/* gnu17 only */\nint compute(int);\nint counter;\nvoid f(void) { int x = compte(1); }\nvoid g(void) { int d = countr; }\n"
        "void k(void) { { countre(1); } int e = countr; }\nvoid m(void) { countre(1); int e = countr; }\n"
        // No search where the name was declared with linkage before, another call's or a block's extern (ExternCPrev).
        "int process(int);\nvoid n(void) { proces(1); }\nvoid o(void) { proces(2); }\nvoid q(void) { extern int procss(int); }\n"
        "void r(void) { procss(3); }\n",
        // A library function's implicit declaration stays a candidate after its block, as clang's builtin is declared at
        // file scope; a nearer or as near name of the block still decides (review of #1822's gaps).
        "void f(void) { printf(\"x\"); { puts(\"y\"); } }\nvoid h(void) { int x = printff; int y = putz; (void)x; (void)y; }\n"
        "void k(void) { int printf_ = 0; int w = printff; (void)printf_; (void)w; }\nvoid m(void) { int puts = 1; int v = putz; (void)v; }\n",
        // Later diagnostics spell the name a correction took, and a warning at the declarator or the operator whose operand
        // holds it comes after the correction's error.
        "const int counter = 1;\nint icount;\n__attribute__((deprecated)) int olden;\nvoid take(int *);\nvoid takeu(unsigned *);\n"
        "void f(void)\n{\n    countr = 5;\n    ++countr;\n    int y = olde;\n    int *p = &countr;\n    unsigned *u = &icoun;\n"
        "    take(&countr);\n    takeu(&icoun);\n    int *q;\n    q = &countr, q = 0;\n    int *r = 0, *s = &countr;\n"
        "    (void)y; (void)p; (void)u; (void)q; (void)r; (void)s;\n}\n",
        // A corrected use's warnings stay in a unit with another error (check_driver_rejected_recovered): an array index's
        // at the function's end, after the body's later errors, or at a file-scope declarator's end, after its correction's
        // error; a division's. An operand of an uncorrected name's expression gives none, as clang's, nor does pointer
        // arithmetic past the end (-Warray-bounds-pointer-arithmetic, off by default).
        "int arr[3];\nint *p = &arx[5], *q = &arx[7];\nint f(void)\n{\n    int x = arx[5];\n    int *r = &arx[4];\n    int *s = arx + 5;\n"
        "    int y = zzz;\n    return x + y + *r + *s + arx[-1];\n}\nint g(void) { return yyy + arx[3]; }\n"
        "int h(void) { return arx[1] / 0 + 1 / 0; }\n",
        // Keywords within an expression (#1447): an expression's and, within a function, a statement's, `break` and
        // `continue` in a loop's body, `case` and `default` in a switch's, a statement's none before a `)`; a value as near
        // leaves the name uncorrected, and one taken where no keyword is offered corrects it. C23's `true` and `false`.
        "void take(int);\nint whilf;\nint g(int a)\n{\n    int x = whil + 1;\n    int y = retrn;\n    int z = (whil);\n    int w = sizeo * 2;\n"
        "    int arr[2] = { retrn, a };\n    take(sizeo);\n    take(retrn);\n    take(fals);\n"
        "    for (;;) { a = brek; a = contnue + undeclared_one; }\n    switch (a) { case 1: a = defaul; a = cas; }\n    a = brek;\n"
        "    do { a = contnue; } while (contnue + 1);\n    for (a = brek; a < 2; a += contnue) { a = 3; }\n    int n = fals;\n"
        "    return a + x + y + z + w + n + arr[0];\n}\nint h = retrn;\n",
        // Type names in an expression: the first name in parentheses and a cast's operand take a typedef's name or a type
        // keyword (clang's MaybeTypeCast), which is suggested, and a cast's operand no value.
        "typedef int myint;\nint vary;\nint f(int a)\n{\n    int b = (myin + 1);\n    int c = sizeof(myin);\n    int d = (int)myin;\n"
        "    int e = (unsignd + 1);\n    int g = (int)retrn;\n    int h = (int)vari;\n    int i = (a + unsignd);\n    int j = (myin);\n"
        "    int k = (unsignd);\n    int l = sizeof(myin) + __alignof__(myin);\n    return b + c + d + e + g + h + i + j + k + l;\n}\n",
        // A statement's start (#1447, check_driver_typo_edits): a name clang's parser corrects to a keyword is read again as
        // that keyword (`retrn x;` returns x), with no error of the parse's; a tie (retrm) or a keyword not offered there
        // (`brek` outside a loop) leaves it undeclared, and a statement whose name stays undeclared is skipped to its `;`
        // (`retrn a + 1`). Before C23 an undeclared function's call is no typo (`whil (a)`); C23 reads it as the keyword.
        "int counter;\nvoid v(void)\n{\n    retrn;\n}\nint f(int x)\n{\n    retrn x;\n}\nint g(int a)\n{\n    int retrm;\n    retrn;\n"
        "    retrn a + 1;\n    brek;\n    for (;;) brek;\n    swich (a) { a = 2; }\n    iff (a) return 1;\n    whil (a) a--;\n    int unused;\n"
        "    return a;\n}\nint h(int a) { int u2; if (a = 1) return 2; return counte; }\n",
        // The correction is its scope's error: the scope's unused variable is quiet, as clang's is.
        "int f(int x)\n{\n    int unused;\n    retrn x;\n}\nint g(int a) { int u2; if (a = 1) return 2; return 0; }\n",
        // A type keyword or a typedef's name corrected there: the statement is a declaration of that type, clang's "unknown
        // type name" with its note for a typedef; before `;` or an operator other than `*` or `&` no type is offered.
        "typedef int myint;\nint f(void)\n{\n    unsignd x = 1;\n    myin y = 2;\n    myin *p = &y;\n    myin;\n    myin + 2;\n"
        "    return x + y + *p;\n}\nint g(int a) { int unused; if (a = 1) return 2; return 0; }\n",
        // A parameter's unknown type name (#1447, check_input_parameter_typo): clang's "unknown type name" with its
        // suggestion, a typedef's (a block's own too) or a type's keyword, and the parameter read with it; every
        // declaration's, a prototype's and a definition's, in one reading. A name with no suggestion keeps its error alone.
        "typedef int myint;\nint a(unsignd v);\nint b(constt int c);\nvoid g(void) { typedef long blk; int h(blkk q); }\n"
        "int d(myin v, myin w) { return v + w; }\nint e(myin *p, int n);\nint f(int n, myin p[n]);\nstruct S { int q; };\n"
        "typedef struct S Sty;\nint i(Stx s) { return s.q; }\nint k(nosuchtype v);\n",
        // A parenthesized name before an operand (`(myin)x`, clang's MaybeTypeCast): "use of undeclared identifier" with a
        // typedef's suggestion, the expression invalid and its rest skipped (check_driver_typo_tail); `(myin)(x)` is the
        // same name's call.
        "typedef int myint;\nvoid n(int x) { char *p = (myin)x; }\nvoid m(int x) { int q = (myin)(x); }\n"
        "void k(int x) { int q = (myin)x + \"a\"; }\nvoid j(int x) { int q = (myin)x, r = (myin)x; }\n"
        "void i(int x) { int q = (nosuchthing)x; }\nint g(int a) { int unused; if (a = 1) return 2; return 0; }\n",
        // A name before an operand within an expression (`sizeo a`, `retrn 5`): its keyword's suggestion, and the
        // expression's rest skipped to its `,`, `;` or closer without the parser's error (check_driver_typo_tail).
        "void take(int, int);\nint g(int a)\n{\n    int w = sizeo * 2;\n    int v = sizeo a b, k = zzz1;\n    take(sizeo a, zzz2);\n"
        "    a = sizeo a + zzz3; zzz4;\n    if (sizeo a) zzz5;\n    a = 1 + sizeo \"abc\"; zzz6;\n    int y = retrn 5;\n    return sizeo a + zzz7;\n"
        "}\nint h(int a) { int unused; if (a = 1) return 2; return 0; }\n",
        // A name near a keyword before an operand that no search corrects (whilx is as near as while): clang's expression is
        // invalid all the same, its rest skipped without the parser's error.
        "int whilx;\nint f(int a)\n{\n    int v = whilz a;\n    return v;\n}\n",
        // An undeclared name in an attribute's argument (#2251): clang's error at it, after which its parser drops the
        // attribute, the list's others stand, and the declaration and the unit go on: a generic attribute's, a known one's
        // expression (aligned, alloc_size, cleanup), a member's and a block-scope declarator's; a library function's name
        // is still declared implicitly there.
        "void *a1(void *d) __attribute__((diagnose_as_builtin(nosuch, 1)));\nvoid *p = memset;\n"
        "int g(int) __attribute__((alloc_size(nosuch), unused));\nint v __attribute__((aligned(zzz))), w __attribute__((aligned(8)));\n"
        "void h(void) __attribute__((cleanup(qq)));\nstruct S { int m __attribute__((aligned(yyy))); int n; } s;\n"
        "void k(void) { int l __attribute__((aligned(xxx))) = 1; int unused; (void)l; }\nint y = 1/0;\n"
        "int f(void) { return undeclared_fn(1); }\n",
        // Statement checks in clang's words (#2406): a `case` or a `default` outside any switch at its keyword; a subscript of
        // a function's pointer at that operand, the pointee's typedef as written; and of neither an array, a pointer nor a
        // vector at its `[`.
        "typedef int fn(void);\nint h(void); fn *p; int c; struct S { int a; } s; double d;\nvoid k(void)\n{\n    int unused;\n"
        "    h[3];\n    p[1];\n    3[h];\n    c[0];\n    0[c];\n    s[1];\n    d[2];\n    { case 1: c = 2; }\n    default: c = 3;\n}\n"
        "int g(int a) { int u2; if (a = 1) return 2; return 0; }\n",
        // A member access whose base is no structure or union (#2288): clang's "member reference base type ... is not a
        // structure or union" at the operator, the type as written for `.`; `.` of a pointer to one is "is a pointer; did you
        // mean to use '->'?" there, and `->` of no pointer "member reference type ... is not a pointer" at the member's name.
        "typedef int myint;\nstruct S { int a; } s, *sp;\nunion U { int u; } uu;\nint z = sizeof(((int *)0)->a);\n"
        "int f(int x, int *p, double d, myint m)\n{\n    int unused;\n    x = p->a + (1, 2);\n    d.re = 1;\n    uu.v = 2;\n"
        "    return x.a + sp.a + x->b + sizeof(d.k) + m.c + s.zz + sp->yy;\n}\nint g(int a) { int u2; if (a = 1) return 2; return 0; }\n",
        // Any name that stays undeclared before an operand (#2255): clang's "use of undeclared identifier", its expression
        // invalid and its rest skipped as a keyword's typo's is, and the unit goes on. Before C23, `u8'a'` is the name `u8`
        // and a character constant.
        "/* gnu17 only */\n_Static_assert(u8'a' == 0, \"20\");\n_Static_assert('a' == 0, \"next\");\nint x = u8'b';\n"
        "int f(void) { int y = u8'c'; return y + u8 'd' + 1; }\nint z = 1 / 0;\nint g(int a) { int unused; if (a = 1) return 2; return 0; }\n",
        "int x = nosuch 5;\nint f(int a) { return (zzz a) + 1; }\nvoid h(int a) { a = yyy \"s\" + 2; www 1, a; }\n_Static_assert(0, \"after\");\n"
        "int y = 1 / 0;\nint g(int a) { int unused; if (a = 1) return 2; return 0; }\n",
        // A block item's GNU attributes before a name that a correction takes as a value (#1447, check_driver_typo_tail):
        // clang's typo error, then "expected identifier or '('" there, the item skipped; the body's unused variable is quiet.
        "int counter;\nvoid f(int n)\n{\n    int value;\n    __attribute__((unused)) valu = 1;\n"
        "    __attribute__((unused)) __attribute__((cold)) valu;\n    __attribute__((unused)) countr = 2;\n    int unused;\n"
        "    if (n = 1) value = 2;\n}\nint h(int a) { int u2; if (a = 1) return 2; return 0; }\n",
        // A name no correction takes is still an implicit int's declarator, whose error ends the body here.
        "int value;\nvoid g(void)\n{\n    __attribute__((unused)) zzzz = 2;\n    __attribute__((unused)) valu = 3;\n}\n",
        // Three thousand names and fifty-three typos, written out where the fixture is (check_test_typo_stress): each
        // search reads the visible names once (c_check_typo.c).
        "/* typo stress */\n",
        // An undeclared name in an enumerator's value or a bit-field's width (#1447, check_constant_name): clang's error at
        // each name, with its typo's correction, which the value takes. With none, the enumerator takes the value it would
        // have had without one (B, D, S), and the member isn't a bit-field: an unnamed one stays an unnamed member of its
        // whole type (T's and U's `long`). The unit's later declarations and warnings stay; at block scope too.
        "enum { WIDTH = 3, ALPHA = 1, BETA = ALPA + 1 };\nenum E { Z = 4, A = zzz, B, C = yyy + www, D = sizeof(xxx) };\n"
        "enum F : unsigned char { R = vvv, S };\nstruct S { int b : uuu; int c; unsigned d : WIDTHX; unsigned : ttt; };\n"
        "struct T { char a; long : sss; char b; } t = { 1, 2 };\n"
        "struct U { struct { long : rrr; int x; }; char y; } u = { { 1 }, 2 };\n"
        "_Static_assert(B == 6 && D == 8 && S == 1 && BETA == 2, \"\");\n"
        "_Static_assert(sizeof(struct S) == 16 && sizeof(struct T) == 24 && _Alignof(struct T) == 8 && sizeof(struct U) == 24, \"\");\n"
        "int f(int n) { int unused; if (n = 2) return B; return C; }\n",
        "enum { LIMIT = 8 };\nint f(int n)\n{\n    enum { A = zzz, B, C = LIMT + 1 };\n"
        "    struct L { int b : yyy; int c; unsigned d : LIMTI; unsigned : xxx; } l = { 1, 2, 3 };\n"
        "    if (n = 2) return B + C + l.c;\n    return sizeof(struct L);\n}\n"
        "int g(int n) { int unused; if (n = 2) return 1; return 0; }\n",
        // An undeclared name in a typeof operand, an array's bound or a _BitInt's width (#1447, check_constant_name): clang's
        // error at each name, with its typo's correction, which the declaration takes (y has counter's type, v is a VLA).
        "int counter;\nenum { WIDTH = 8 };\ntypeof(countr) y;\nint *py = &y;\n_BitInt(WIDTHX) w;\nint f(int n)\n{\n"
        "    int unused;\n    int v[countr + n];\n    if (n = 2) return sizeof(typeof(countr)) + sizeof v;\n    return (int)w;\n"
        "}\n",
        // One that no correction takes is clang's error at each name of its expression, in its order: a bound's, a
        // typedef's, a type name's typeof's, and a designator's (whose list clang drops there). A
        // _BitInt's is no type specifier, as clang drops it: its parser's error follows, a declaration's at its first
        // declarator's name (C23's own words, and a C23 parameter's name a type's), a member's or a type name's at the end
        // of the list.
        "int a[zzz + yyy];\n",
        // A bound whose undeclared name isn't its first token: the name's error alone (review on #1885).
        "int a[1 + zzz];\n",
        "typedef int T[zzz];\n",
        "int f(void) { return sizeof(typeof(zzz)); }\n",
        "_BitInt(zzz) const *volatile p, q;\n",
        "int f(_BitInt(zzz) a[3]);\n",
        "struct S { _BitInt(zzz) m; };\n",
        "int s = sizeof(_BitInt(zzz));\n",
        "int a[3] = { [zzz] = 1, [yyy] = 2 };\n",
        "enum { W = 1 };\nint a[3] = { [W ... zzz + yyy] = 1 };\n",
        // A declaration's last declarator whose bound or typeof has undeclared names that no correction takes is invalid,
        // as clang's, and stays bound, its uses quiet (#1447, check_input_declarator_recover_names, the specifiers'
        // typeof): the unit goes on with its later errors and warnings, at file scope and in a body. A typedef's, a type
        // name's or a designator's still ends the walk, and so does a declarator before another, whose rest clang's parser
        // skips (`int a[zzz], b;` declares no b).
        "typeof(zzz) x; int *p = &x; int y(void) { return x.foo + sizeof x; }\n",
        "int a[zzz]; int *p = a; int f(void) { int u; return sizeof a + a[1]; }\n",
        "int f(void) { int v[zzz]; return v[0] + nope; }\n",
        "int f(void) { typeof(zzz) v; int u; return v + nope; }\n",
        "int a[zzz]; int b[yyy]; int g(int n) { if (n = 2) return 1; return 0; }\n",
        "typeof(zzz) x; typeof(yyy) w; int f(void) { int unused; return 0; }\n",
        "int a[zzz]; int a[3];\n",
        "static int a[zzz]; int f(void) { return a[0]; }\n",
        // A declarator's constant that fails ends the walk with clang's error in its words (#1447,
        // check_input_constant_failure_report), as an attribute argument's: a missing member, `->` of no pointer and a
        // member access's base, pointer arithmetic on an incomplete pointee, `sizeof` of an incomplete type, a call's
        // argument count (too many at the first extra one) and a binary operator's operands; in a bound, a typedef's, a
        // typeof, an enumerator's value, a bit-field's width, a _BitInt's, a designator, and the attributes' own; in a body
        // too; and a failure before a bound's undeclared name, both in source order.
        "struct S { int a; } s; int a[s.b];\n",
        "struct S { int a; } s; typedef int A[s.b];\n",
        "int p; int a[p->x];\n",
        "int *q; int a[q->x];\n",
        "struct T; int a[sizeof(1 + (struct T*)0)];\n",
        "struct S{int a;} s; typeof(s.b) x;\n",
        "struct S{int a;} s; enum E { A = sizeof(s.b) };\n",
        "struct S{int a;} s; struct R { int b : sizeof(s.b); };\n",
        "struct S{int a;} s; _BitInt(sizeof(s.b)) x;\n",
        "struct S{int a;} s; int a[3] = { [sizeof(s.b)] = 1 };\n",
        "int f(int); int a[f(1, 2)];\n",
        "int f(int, int); int a[f(1)];\n",
        "int f(void);\nint a[f(5)];\n",
        "int f(void);\nint x __attribute__((aligned(f(5, 6))));\n",
        // The forward declaration's note at the diagnosed type's own declaration, which an inner definition shadows, and a
        // sizeof's operand's typedef spelling, as clang's (review on #1900).
        "struct T;\ntypedef struct T Outer;\nvoid f(void) {\n    struct T { int x; };\n    int a[sizeof(Outer)];\n}\n",
        "struct T;\ntypedef struct T *OuterP;\nvoid f(void) {\n    struct T { int x; };\n    int a[sizeof((OuterP)0 + 1)];\n}\n",
        "struct T;\ntypedef struct T Outer;\nvoid f(void) {\n    struct T { int x; };\n    int x __attribute__((aligned(sizeof(Outer))));\n}\n",
        "struct T; int a[sizeof(struct T)];\n",
        "struct S{int a;} s; int a[s + 1];\n",
        "int f(int); int x __attribute__((aligned(f(1, 2))));\n",
        "struct S { int a; } s; int x __attribute__((aligned(s.b)));\n",
        "int p; int y __attribute__((aligned(p->x)));\n",
        "struct T; int x __attribute__((aligned(sizeof(1 + (struct T*)0))));\n",
        "struct S { int a; } s;\nint f(void) { int a[s.b]; return sizeof a; }\n",
        "struct S { int a; } s;\nint f(void) { enum { A = sizeof(s.b) }; return A; }\n",
        "struct S { char bytes[sizeof(struct Missing) -\nsizeof(missing)]; };\n",
        // Declarator errors in clang's words (#1447): a function returning an array or a function, a negative bound
        // (named, and a type name's), an array of functions (by a typedef's name, with its aka) or of an incomplete element
        // (with the tag's forward declaration's note), and a variable of type void or of an incomplete tag, a tentative
        // definition's too. Each declaration is invalid and stays bound, its uses quiet (check_input_invalid_declare).
        "struct S;\nint f(void)[3];\n",
        "typedef int F(void);\nF g(void);\n",
        "int s = sizeof(int[2]);\nint a[-1];\n",
        "int s = sizeof(int[-4]);\n",
        "typedef int F(void);\nF *ok[2];\nF af[2];\n",
        "struct S;\nvoid h(struct S a[]);\n",
        "int b[2][];\n",
        "static void v;\n",
        "struct S;\nint k(void) { struct S local; return 0; }\n",
        "struct S s;\nint ok(void) { return 0; }\n",
        // An array past clang's object limit, at its bound, its count with clang's digit separators: a declaration's, an
        // inner array's (whose outer one is past it) and a type name's.
        "char ok[0x1fffffffffffffff];\nint a[0x2000000000000000];\n",
        "int c[0x100000000][0x100000000];\n",
        "int s = sizeof(int[0x4000000000000000]);\n",
        // A parameter's bound past its `static` and qualifiers (review on #1819).
        "void f(int a[static 0x2000000000000000]);\n",
        "void g(int a[const volatile 0x2000000000000000]);\n",
        // A tag defined in an _Atomic's or a typeof's type name (#1447), nested too, read before the specifiers: at file
        // scope, in members, blocks, typedefs and type names, its type usable after it; the unit's error is a later one.
        "_Atomic(struct A { int a; char c; }) m;\n_Atomic(union U { int a; float f; }) u;\n_Atomic(enum E { E1, E2 }) e;\n"
        "typeof(struct T { long t[3]; }) const t;\ntypedef _Atomic(struct B { long b; }) AB;\n"
        "typeof(_Atomic(struct Z { short z; })) zz;\n_Static_assert(sizeof zz == 2 && _Alignof(_Atomic(struct A)) == 8, \"\");\n"
        "struct H { _Atomic(struct I { int i; }) inner; typeof(struct J { char j; }) jj; };\n"
        "int f(void) { _Atomic(struct L { int l; }) x; typeof(struct M { int m; }) y = {1}; "
        "return sizeof(_Atomic(struct N { int n; })) + (int)sizeof(typeof(struct O { int o; })) + y.m + E2; }\n"
        "int g(void) { return sizeof(struct A) + sizeof(struct I) + sizeof(enum E) + sizeof(struct J) + t.t[1] + missing; }\n",
        "const _Atomic(struct A { int a; }) *p, q;\n_Atomic(struct A) r;\nint k(void) { int unused; return (int)sizeof(*p) + undeclared; }\n"
        "void l(void) { for (_Atomic(struct F { int f; }) i;;) { (void)i; break; } int quiet; }\n",
        // In a prototype's parameters (review on #1891): a tag of prototype scope, with clang's -Wvisibility before C23, in a
        // declaration's parameter, a pointer's, a nested group's and a definition's.
        "void f(_Atomic(struct P { int x; }) p);\nvoid g(typeof(struct Q { long y; }) *q);\nvoid h(typeof(_Atomic(struct R { short z; })) r);\n"
        "int k(_Atomic(struct A { int a; }) a, typeof(struct B { char b; }) b) { int unused; return (int)sizeof a + b.b; }\nint z = missing;\n",
        // ...whose tag stays the prototype's or the definition's: a later file-scope use names another, incomplete one (second
        // review on #1891), and a file-scope definition of its name is no redefinition.
        "void f(typeof(struct T { int x; }) *p);\nstruct T *global;\nint n = sizeof(struct T);\n",
        "void f(typeof(struct T { int x; }) *p) { (void)p; }\nstruct T *global;\nint n = sizeof(struct T);\n",
        "void f(typeof(struct T { int x; }) *p);\nstruct T { long y; } t;\nint n = sizeof(struct T);\nint g(void) { int unused; return missing; }\n",
        // Invalid declarations in a row, whose uses are quiet: in later bodies, which keep their -Wall warnings, in a body's
        // own invalid declarations, and in constant contexts (a bound, typeof, an enumerator, _Static_assert, a bit-field's
        // width, a case label), which read a stand-in int.
        "struct S;\nint a[-1];\nint f(void)[3];\ntypedef int F(void);\nF af[2];\nstruct S s;\nstatic void v;\n"
        "int use1(void) { return a[0] + f()[1] + af[0](); }\nint use2(void) { int unused; return sizeof s; }\nint b[sizeof a];\n"
        "enum { E = sizeof a };\n_Static_assert(sizeof a, \"\");\nint w(int x) { switch (x) { case sizeof a: return 1; } return 0; }\n",
        "struct S;\nint a[-1];\nint b[sizeof a];\ntypeof(a) t;\nenum { E = sizeof a };\n_Static_assert(sizeof a, \"\");\n"
        "struct B { int w : sizeof a; };\nint w(int x) { switch (x) { case sizeof a: return 1; } return 0; }\n",
        "int a[-1];\nint g(void) { int unused; return a[0]; }\nint h(void) { int unused2; return 0; }\n"
        "void m(void) { int c[-2]; int unused3; c[0] = 1; float y = c; }\n",
        "struct S;\nvoid m(void) { int c[-2]; int unused3; c[0] = 1; }\nint g(void) { int u; struct S local; return sizeof local; }\n"
        "int h(void) { int u2; return 0; }\nint k(int q) { void v; int *p = &v; return q; }\n",
        "int f(void)[2] { int unused; return 0; }\nint g(void)[3];\nint use(void) { int x = g()[1]; return x; }\ntypedef int F(void);\n"
        "F af[2];\nint call(void) { return af[0](); }\nint c[0x100000000][0x100000000];\nint cu(void) { return c[1][2]; }\n",
        "int ok(void) { return 1; }\nvoid body1(void) { int c[-2]; c[0] = 1; int u; }\nvoid body2(void) { int fine; }\n"
        "void body3(void) { struct Q q; (void)q; }\nint tail(int z) { if (z = 3) return 1; return 0; }\n",
        // A redeclaration after an invalid one: an object's is checked as if nothing were visible, but for its linkage; a
        // function's is invalid too, its uses as quiet.
        "int a[-1]; int a[2]; float a;\nstruct S s1; int s1; float s1;\nstatic void v; int v;\nint f(void)[3]; int f(void); float f(int);\n"
        "int u(void) { return f(1, 2); }\nint y(void) { int unused; return 0; }\n",
        "int f(void)[3]; int f(void); float f(int);\nint u(void) { return f(1, 2); }\nint y(void) { int unused; return 0; }\n",
        // A function's redefinition is clang's invalid declaration, kept in its chain (#2362): reported once, a third or
        // a later declaration quiet, of any type, and so are its uses, a block's extern one's too; its body's errors and
        // another kind's redeclaration (noted at it) are still reported.
        "int g(void) { return 1; }\nint g(void) { return 2; }\nint g(void) { return 3; }\nlong g(void);\nint h(void) { return g(1); }\n"
        "int k(void) { extern int g(void); return g(2); }\nint (*p)(int) = g;\nint y(void) { int unused; return undeclared; }\n",
        "static int g(int a) { return 1; }\nstatic int g(int a) { return undeclared; }\nint g;\nint h(void) { return g(); }\n",
        // Another kind of entity's redeclaration after an invalid one is still clang's "redefinition of 'a' as different
        // kind of symbol" (review on #1884): an object after a function, a function after an object, and a typedef after
        // either.
        "int a[-1];\nint a(void);\nint f(void)[3];\nint f;\nstruct S;\nstruct S s;\nvoid s(void);\nint g(void) { int unused; return a + f; }\n",
        "static void v;\nint v(void);\nint w(void)[2];\ntypedef int w;\nint b[-1];\ntypedef int b;\nint h(void) { int unused; return 0; }\n",
        // C23's redefinition of an enum at file scope (#1447, N3037): a compatible one is silent, and its names unambiguous
        // after it; an incompatible one is clang's "incompatible definitions" at its name, with the first pair of its
        // enumerators sorted by name that differ in name or value, or one past the other list and "no corresponding
        // enumerator here" at the other's name. A name both lists declare is ambiguous within the second body, and after an
        // incompatible one, with its candidates; a body with such a use keeps no unused variable, the others do.
        "/* gnu23 only */\nenum E { A, B };\nenum E { A, B };\nint x = A;\nenum E e = B;\nenum F { F1 } f1;\nenum F { F1 } f2;\n"
        "enum H : int { H1 };\nenum H : int { H1 };\nenum K { K1 = 5, K2 };\nenum K { K1 = 5, K2 = 6 };\nint y = undeclared;\n",
        "/* gnu23 only */\nenum G { X, Y };\nenum G { X, Z };\nint v = Y + Z;\nint q = sizeof(enum G);\nint g(void) { int u2; return 0; }\n",
        "/* gnu23 only */\nenum G { X, Y };\nenum G { X, Z };\nint u = X;\nvoid f(void) { int unused; int k = X; }\nint g(void) { int u2; return 0; }\n",
        "/* gnu23 only */\nenum G { X, Y };\nenum G { X };\nint v = Y;\nenum M { M1 };\nenum M { M1, M2 };\n"
        "enum P : int { P1 };\nenum P : int { P1, P2 };\n",
        "/* gnu23 only */\nenum G { X = 1 };\nenum G { X = 2 };\nenum S { S1, S2 };\nenum S { S2, S1 };\nenum T { zz, B, a };\n"
        "enum T { zz, B, A };\nenum N { N1 = -1, N2 };\nenum N { N1 = -2, N2 };\nenum U { U1 = 0xffffffffu };\nenum U { U1 = 0xfffffffeu };\n",
        "/* gnu23 only */\nenum G { X, Y };\nenum G { X, Z = X + 1 };\nenum R { R1, R2 = R1 + 1 };\nenum R { R1, R2 = R1 + 1 };\n"
        "int q = R2;\nint w(void) { int unused; return 0; }\n",
        // A name only the dropped redefinition declares, though a local of another body or a later declaration spells it
        // too (review on #1928): its uses still mask their bodies in the later readings, so the unit keeps its warnings.
        "/* gnu23 only */\nenum E { X, Y };\nenum E { X, Z };\nvoid f(void) { int Z; }\nint g(void) { return Z; }\n"
        "int h(void) { int unused; return 0; }\nvoid k(void) { int m = X; }\n",
        "/* gnu23 only */\nenum E { X, Y };\nenum E { X, Z };\nint g(void) { return Z; }\nint h(void) { int unused; return X; }\n"
        "struct Z { int a; };\nint i(void) { int u2; return 0; }\n",
        // A deprecated declaration's uses before a parse error (#2202): the view of the unit that the error leaves (a
        // prefix, a repair) gives their -Wdeprecated-declarations, with the attribute's message and the note: an object's,
        // a function's, a typedef's and a tag's, at file scope and in bodies.
        "int w __attribute__((deprecated)); int v = w; int z = ;\n",
        "int w __attribute__((deprecated)); int f(void) { return w; } int z = ;\n",
        "int w __attribute__((deprecated(\"old\"))); void g(int) __attribute__((deprecated));\nint f(void) { g(w); return w; }\nvoid h(void) { g(1);\n",
        "typedef int T __attribute__((deprecated));\nstruct S { int m; } __attribute__((deprecated));\nT t; struct S s;\nint f(void) { T x = 1; return x + t; }\n"
        "int z = ;\n",
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
    // The tag redefinitions, the last three fixtures, are gnu17's alone: C23 compares a redefined record or enum with the
    // earlier one (N3037, the gnu23 fixtures above).
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
        // A fixture that starts with this marker is gnu23's alone (C23's tag compatibility).
        const bool c23_only = strncmp(fixtures[f], "/* gnu23 only */", 16) == 0;
        for (uint32_t s = c23_only ? 1u : 0u; made && s < standard_count; s += 1)
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
