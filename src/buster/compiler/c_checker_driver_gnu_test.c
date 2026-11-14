// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #147: GNU C's local labels and __auto_type against clang, through the driver: each fixture's first error at clang's
// line and column, and with clang's text where the driver's message is clang's; and where a unit has several label
// errors, every one, in clang's order, with their notes: an ambiguous name's where it is met, its candidates in clang's
// lookup's order, a list's undeclared labels as its block ends and the function's (each ambiguous reference's label among
// them) as the function does.
#ifndef BUSTER_C_CHECKER_DRIVER_GNU_TEST_INCLUDED
#define BUSTER_C_CHECKER_DRIVER_GNU_TEST_INCLUDED

// The error and note lines of `text` (`file:line:column: error: message`), in their order, into `errors` with a newline
// after each. Returns false for more lines than it keeps or than `errors` has room for.
static bool check_test_driver_gnu_errors(char* text, char* errors, size_t capacity)
{
    char* lines[64];
    uint32_t count = 0;
    bool valid = text != NULL && capacity != 0;
    for (char* at = text; valid && at != NULL && *at != 0;)
    {
        char* end = strchr(at, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        if (strstr(at, ": error: ") != NULL || strstr(at, ": note: ") != NULL)
        {
            valid = count < BUSTER_ARRAY_LENGTH(lines);
            lines[valid ? count : 0] = at;
            count += valid;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    size_t length = 0;
    errors[0] = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const int written = snprintf(errors + length, capacity - length, "%s\n", lines[i]);
        valid = written >= 0 && (size_t)written < capacity - length;
        length += valid ? (size_t)written : 0;
    }
    return valid;
}

static void check_test_driver_gnu_scopes(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
        bool exact_message;
    } fixtures[] =
    {
        { "a local label its block doesn't define", "void f(void) {\n    {\n        __label__ missing;\n        (void)0;\n    }\n}\n", true },
        { "a goto from outside a local label's block", "void f(void) {\n    goto inner;\n    {\n        __label__ inner;\n    inner:;\n    }\n}\n", true },
        { "a local label defined twice", "void f(void) {\n    {\n        __label__ a;\n    a:;\n    a:;\n    }\n}\n", true },
        { "a function's label used after a local one's block", "void f(void) {\n    {\n        __label__ a;\n    a:;\n    }\n    goto a;\n}\n", true },
        { "an inner list's label its block doesn't define", "void f(void) {\n    __label__ a;\n    {\n        __label__ a;\n        goto a;\n    }\na:;\n}\n", true },
        { "__auto_type without an initializer", "void f(void) {\n    __auto_type\n        x;\n}\n", true },
        { "__auto_type's initializer names its variable", "void f(void) {\n    int x = 1;\n    {\n        __auto_type x =\n            x + 1;\n    }\n}\n", true },
        { "__auto_type's initializer names it in sizeof", "void f(void) {\n    __auto_type x = sizeof(x);\n}\n", true },
        { "__auto_type's initializer names it in a type query", "void f(void) {\n    __auto_type x =\n        sizeof(__typeof__(x));\n}\n", true },
        { "__auto_type's initializer names it in a compound literal's bound", "void f(void) {\n    __auto_type x =\n        (int[sizeof(x)]){0}[0];\n}\n", true },
        { "__auto_type's initializer names it in another's", "void f(void) {\n    __auto_type a = ({\n        __auto_type b = a;\n        b; });\n}\n", true },
        { "__auto_type's initializer names it in a nested type query", "void f(void) {\n    __auto_type x =\n        sizeof(int[sizeof(__typeof__(x))]);\n}\n", true },
        { "an assignment to a const __auto_type", "void f(void) {\n    const __auto_type x = 1;\n    x =\n        2;\n}\n" },
        { "__auto_type with an initializer list", "void f(void) {\n    __auto_type x =\n        { 1 };\n}\n", true },
        { "__auto_type with an array", "void f(void) {\n    __auto_type a[] =\n        { 1, 2 };\n}\n", true },
        { "__auto_type of a bit-field", "struct B { int b : 3; };\nvoid f(struct B s) {\n    __auto_type x =\n        s.b;\n}\n", true },
        { "__auto_type as a parameter", "void f(int a,\n       __auto_type b);\n" },
        { "__auto_type as a function's result", "__auto_type\nf(void);\n" },
        { "__auto_type in a typedef", "typedef\n    __auto_type T;\n" },
        { "__auto_type in a cast", "int f(void) {\n    return (\n        __auto_type)1;\n}\n" },
        { "__auto_type of a void value", "void f(void) {\n    __auto_type\n        x = (void)0;\n}\n" },
        { "__auto_type with a signedness", "void f(void) {\n    long\n        unsigned __auto_type x = 1;\n}\n" },
        { "__auto_type after int", "void f(void) {\n    int\n        __auto_type x = 1;\n}\n" },
        { "_Complex __auto_type", "void f(void) {\n    __auto_type\n        _Complex x = 1;\n}\n" },
        { "__auto_type with an _Alignas below the deduced type's", "void f(void) {\n    _Alignas(4) __auto_type x =\n        1.0;\n}\n" },
        { "__auto_type before an _Alignas below a pointer's", "void f(void) {\n    char a[3];\n    __auto_type _Alignas(1) p = a;\n}\n" },
        // A vector's or a mode's attribute meets the undeduced placeholder (#1844): clang's errors at the attribute, the
        // placeholder's type as the attribute's place has it (the specifiers' bare one, the declarator's whole one), a
        // vector's before a mode's.
        { "vector_size after an __auto_type's name", "void f(void) {\n    __auto_type a\n        __attribute__((vector_size(16))) = 1;\n}\n", true },
        { "vector_size of a const __auto_type pointer", "void f(void) {\n    const __auto_type *a __attribute__((vector_size(16))) = 0;\n}\n", true },
        { "vector_size of an _Atomic __auto_type", "void f(void) {\n    _Atomic __auto_type a __attribute__((vector_size(16))) = 1;\n}\n", true },
        { "vector_size of a restrict __auto_type", "void f(void) {\n    __auto_type restrict a __attribute__((vector_size(16))) = 1;\n}\n", true },
        { "vector_size among __auto_type's specifiers", "void f(void) {\n    const __attribute__((vector_size(16))) volatile __auto_type a = 1;\n}\n", true },
        { "ext_vector_type of a file-scope __auto_type", "__auto_type g __attribute__((ext_vector_type(4))) = 1;\n", true },
        { "mode of an __auto_type", "void f(void) {\n    __auto_type b __attribute__((mode(DI))) = 1;\n}\n", true },
        { "mode among __auto_type's specifiers", "void f(void) {\n    __attribute__((mode(DI))) __auto_type b = 1;\n}\n", true },
        { "a mode before a vector of an __auto_type", "void f(void) {\n    __auto_type a __attribute__((mode(DI), vector_size(16))) = 1;\n}\n", true },
    };
    const char* arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/gnu-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/gnu-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected, fixtures[i].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u, clang %u:%u", fixtures[i].name, route,
                     checked.line, checked.column, oracle.line, oracle.column);
            check_test_expect(test, ran && checked.rejected && checked.line == oracle.line && checked.column == oracle.column, description);
            snprintf(description, sizeof(description), "%s: first diagnostic text '%s', clang's '%s'", fixtures[i].name, checked.message, oracle.message);
            check_test_expect(test, !fixtures[i].exact_message || strcmp(checked.message, oracle.message) == 0, description);
            check_test_expect(test, access(object, F_OK) != 0, "a rejected unit writes no object");
        }
        unlink(object);
        unlink(source);
    }
    // A name a block's lists declare twice: clang's ambiguity where it is defined or used (and its undeclared label at a
    // use, as the function ends), and an undeclared label at each declaration, as the block ends; with a name they declare
    // once and its block doesn't define; and a function's missing labels, once a name, as the function ends.
    static const struct
    {
        const char* name;
        const char* source;
        const char* file;
    } every[] =
    {
        { "a name one list declares twice, defined", "void f(void) {\n    {\n        __label__ a, a;\n    a:;\n    }\n}\n" },
        { "a name two lists declare twice, a goto's", "void f(void) {\n    {\n        __label__ a;\n        __label__ a;\n        goto a;\n    }\n}\n" },
        { "a name declared twice, an address's", "void f(void) {\n    {\n        __label__ a, a;\n        void *p = &&a;\n        (void)p;\n    }\n}\n" },
        { "a name declared twice, used and defined", "void f(void) {\n    {\n        __label__ a, a;\n        goto a;\n    a:;\n    }\n}\n" },
        { "a name declared twice, never used", "void f(void) {\n    {\n        __label__ a, a;\n    }\n}\n" },
        { "a list's two names, one undefined", "void f(void) {\n    {\n        __label__ a, b;\n    b:;\n    }\n}\n" },
        { "a name two lists declare twice, defined and used", "void f(void) {\n    {\n        __label__ c;\n        __label__ d, c;\n    c: goto c;\n    d:;\n    }\n}\n" },
        { "an undeclared label's error after the block's", "void f(void) {\n    {\n        __label__ a, a;\n    a:;\n        int y = z;\n    }\n    int w = v;\n}\n" },
        // __auto_type's _Atomic and restrict are its deduced type's (#1844): their types as clang's errors print them.
        { "_Atomic __auto_type's deduced types", "struct T { int a; };\nvoid f(struct T t, int a[3]) {\n    const _Atomic __auto_type q = 1.5;\n    struct S { int x; } s = q;\n"
          "    _Atomic __auto_type r = t;\n    struct S u = r;\n    volatile _Atomic __auto_type v = a, w = a;\n    struct S x = w;\n}\n" },
        { "__auto_type restrict's deduced types", "void f(int *p) {\n    __auto_type restrict q = p;\n    struct S { int x; } s = q;\n"
          "    const __auto_type restrict r = p, t = p;\n    struct S u = t;\n    __auto_type * const restrict v = p;\n    struct S w = v;\n}\n" },
        { "a function's missing labels, once each, as it ends", "void f(void) {\n    goto x;\n    goto y;\n    int u = z;\n    goto x;\n    void *p = &&y;\n}\n" },
        // Clang's lookup makes a label of the function's own scope at each ambiguous reference (#1844): a body's list's
        // entries and those labels are one scope's candidates, in that order (the later made after the earlier); a nested
        // list's ambiguity makes them too, so that a reference of the function's own finds two and more; a definition's is
        // defined, which a later goto finds alone; a goto's is defined by a later definition that finds it alone, and is
        // undeclared otherwise, each one, as the function ends.
        { "a body's list declares a name twice: its candidates and the labels its references make",
          "void f(int a) {\n    __label__ y, y;\n    y:;\n    if (a) goto y;\n    void *p = &&y;\n    (void)p;\n}\n" },
        { "a nested list's ambiguity makes the function's labels",
          "void f(void) {\n    {\n        __label__ w, w;\n        goto w;\n        goto w;\n    }\n    goto w;\n}\n" },
        { "a body's list and the label a nested ambiguity makes",
          "void f(void) {\n    __label__ x;\n    {\n        __label__ x, x;\n        goto x;\n    }\n    goto x;\nx:;\n}\n" },
        { "a function's label before a nested ambiguity's",
          "void f(void) {\n    goto q;\n    {\n        __label__ q, q;\n        goto q;\n    }\n    goto q;\n    void *p = &&q;\n    (void)p;\n}\n" },
        { "an ambiguous definition's label found by a later goto", "void f(void) {\n    {\n        __label__ s, s;\n    s:;\n    }\n    goto s;\n}\n" },
        { "an ambiguous goto's label defined later", "void f(void) {\n    {\n        __label__ t, t;\n        goto t;\n    }\nt:;\n}\n" },
        { "an ambiguous definition's label redefined", "void f(void) {\n    {\n        __label__ s, s;\n    s:;\n    }\ns:;\n}\n" },
        { "a statement expression's ambiguity", "void f(void) {\n    int v = ({ __label__ r, r; goto r; 1; });\n    goto r;\n    (void)v;\n}\n" },
        // An __auto_type redeclaration of an entity with linkage whose type clang's mergeTypes refuses (#1844): the
        // qualifiers of a level differ (the placeholder's, a pointer's, restrict's), a pointer declarator meets no pointer,
        // or an _Atomic placeholder meets no atomic type. Clang's "redefinition" or, with extern, "redeclaration with a
        // different type", the placeholder printed through its declarator, noted at the earlier definition or declaration.
        { "a const __auto_type redefinition of a tentative definition", "int p;\nconst __auto_type p =\n    1;\n" },
        { "a volatile __auto_type definition after an extern declaration", "extern int w;\nconst volatile __auto_type w = 2;\n" },
        { "an __auto_type pointer over a const pointer", "extern int *const b;\n__auto_type *b = (int *)0;\n" },
        { "a const __auto_type pointer's pointee", "int **s;\nconst __auto_type **s = 0;\n" },
        { "a const pointer to a volatile __auto_type", "int *t;\nvolatile __auto_type *const t = 0;\n" },
        { "an __auto_type pointer over no pointer", "int v;\n__auto_type *v = 0;\n" },
        { "a block's extern const __auto_type", "int u;\nvoid f(void) {\n    int u;\n    {\n        extern const __auto_type u = 2;\n    }\n}\n" },
        { "an _Atomic __auto_type over no atomic type", "int p;\n_Atomic __auto_type p = 1;\n" },
        { "a restrict __auto_type over an unrestricted pointer", "int *q;\n__auto_type restrict q = (int *)0;\n" },
        // A cast to an atomic type is an atomic rvalue (#2230), which converts to no other type, which an atomic object of
        // another value type doesn't take, and which the usual arithmetic conversions take on the left alone.
        { "an atomic cast's rvalue converted",
          "int *gp;\nint g(int x, int *p) {\n    int y = (_Atomic int)x;\n    _Bool b = (_Atomic(int *))p;\n    gp = (_Atomic(int *))p;\n"
          "    int *s = (_Atomic(int *))p;\n    int t = (_Atomic(int *))p;\n    _Atomic long u = (_Atomic int)x;\n    return (_Atomic int)x;\n}\n" },
        { "an atomic cast's rvalue as an operand",
          "int g(int x) {\n    int r = 1 + (_Atomic int)x;\n    r = (_Atomic int)x + (_Atomic int)x;\n    r = (_Atomic long)x * 3 + (_Atomic short)x;\n"
          "    return r + (_Atomic int)x * 2;\n}\n" },
        { "a block's non-extern auto-type after extern", NULL, "block-extern.c" },
        { "a block's qualified auto-type after extern", NULL, "block-qualified.c" },
        { "a block's auto-type pointer after extern", NULL, "block-pointer.c" },
        { "auto-type shadowing keeps its own deduction", NULL, "block-shadow.c" },
        { "an atomic auto-type cannot deduce from an atomic rvalue", NULL, "atomic-rvalue.c" },
        { "an atomic auto-type keeps its qualifiers in the error", NULL, "atomic-qualified.c" },
        { "an atomic auto-type cannot deduce an atomic pointer", NULL, "atomic-pointer.c" },
        { "an atomic auto-type retains the initializer's typedef", NULL, "atomic-typedef.c" },
        { "an atomic auto-type retains the initialized object's typedef", NULL, "atomic-typedef-object.c" },
        { "an atomic auto-type retains its initializer's comma warnings", NULL, "atomic-comma.c" },
        { "an invalid atomic auto-type stays bound without cascading diagnostics", NULL, "atomic-recovery.c" },
    };
    for (uint32_t i = 0; executable != NULL && i < BUSTER_ARRAY_LENGTH(every); i += 1)
    {
        char source[192];
        char description[9216];
        snprintf(source, sizeof(source), "%s/gnu-every-%u.c", directory, i);
        CheckTestFixtures fixture = { 0 };
        const char* bytes = every[i].source;
        uint64_t length = bytes != NULL ? strlen(bytes) : 0;
        if (every[i].file != NULL)
        {
            char path[PATH_MAX];
            fixture.storage = arena_reserve(CHECK_TEST_FIXTURE_BYTES + 1);
            snprintf(path, sizeof(path), "%s/checker/auto-type/%s", check_test_fixture_root, every[i].file);
            bytes = check_test_fixture_read(&fixture, path, &length);
        }
        const bool written = bytes != NULL && parse_test_write(source, bytes, length);
        arena_release(&fixture.storage);
        char* const clang_arguments[] = { (char*)clang, "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
        char* text = NULL;
        char expected[4096];
        const bool oracle = written && check_test_driver_spawn(clang_arguments, &text) == 1 && check_test_driver_gnu_errors(text, expected, sizeof(expected));
        free(text);
        check_test_expect(test, oracle && expected[0] != 0, every[i].name);
        for (uint32_t scalar = 0; oracle && scalar < 2; scalar += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
            char errors[4096];
            const bool ran = check_test_driver_spawn(scalar != 0 ? scalar_arguments : vector_arguments, &text) == 1 &&
                             check_test_driver_gnu_errors(text, errors, sizeof(errors));
            free(text);
            snprintf(description, sizeof(description), "%s, %s path: every error clang's:\n%s(ours)\n%s", every[i].name, scalar != 0 ? "scalar" : "vector",
                     ran ? errors : "(none)\n", expected);
            check_test_expect(test, ran && strcmp(errors, expected) == 0, description);
        }
        unlink(source);
    }
    // C23's auto and constexpr (#793), and atomic casts (#2230), are accepted by both paths, as clang accepts them, and must
    // write no diagnostic.
    static const struct
    {
        const char* name;
        const char* source;
    } c23[] =
    {
        { "C23 auto: a pointer's deduction", "int main(void) { __auto_type x = 1; auto p = \"x\"; return p[1] + x; }\n" },
        { "C23 auto: a double's deduction", "int main(void) { auto x = 3.5; return (int)sizeof x - 8; }\n" },
        { "C23 constexpr: an array's bound", "int main(void) { constexpr int y = 2; int a[y]; return sizeof a - 8; }\n" },
        // A cast to an atomic type (#2230) deduced by __auto_type and taken by an _Atomic object, and __auto_type's
        // redeclarations of atomic entities (#1844).
        { "an atomic cast's deduction and an atomic merge",
          "_Atomic int r; __auto_type r = 1; _Atomic int *ap; __auto_type *ap = &r; _Atomic double d; _Atomic __auto_type d = 2.5;\n"
          "static _Atomic int z = (_Atomic int)41;\n"
          "int main(void) { __auto_type a = (_Atomic int)1; _Atomic long b = (_Atomic long)a; (void)(_Atomic int)a;\n"
          "  return a + (int)b + _Generic(&a, _Atomic int *: 0, default: 1) + (int)sizeof((_Atomic int)a) - 6 + r + *ap + (int)d + z - 45; }\n" },
        { "C23 constexpr: a double and a null pointer", "int main(void) { constexpr double d = 2.5; constexpr int *p = nullptr; return (int)d + (p != 0); }\n" },
        // Past the eight values that start check_input_constexpr_find's index (#1381): a file-scope chain, a block-scope
        // constexpr shadowing one of it, and a body's own chain, each read back by a static assertion.
        { "C23 constexpr: chains past the lookup's index",
          "constexpr int c0 = 1; constexpr int c1 = c0 * 3 + 1; constexpr int c2 = c1 * 3 + 1; constexpr int c3 = c2 * 3 + 1;\n"
          "constexpr int c4 = c3 * 3 + 1; constexpr int c5 = c4 * 3 + 1; constexpr int c6 = c5 * 3 + 1; constexpr int c7 = c6 * 3 + 1;\n"
          "constexpr int c8 = c7 * 3 + 1; constexpr int c9 = c8 * 3 + 1;\n"
          "static_assert(c9 == 29524 && c3 == 40);\n"
          "int f(void) { constexpr int c3 = 5; static_assert(c3 == 5 && c4 == 121); return c3; }\n"
          "int g(void) { constexpr int l0 = c9; constexpr int l1 = l0 + 1; constexpr int l2 = l1 + 1; constexpr int l3 = l2 + 1;\n"
          "  constexpr int l4 = l3 + 1; constexpr int l5 = l4 + 1; constexpr int l6 = l5 + 1; constexpr int l7 = l6 + 1; constexpr int l8 = l7 + c3;\n"
          "  static_assert(l8 == 29571 && l0 == 29524); return l8 - 29571; }\n"
          "int main(void) { static_assert(c3 == 40); return f() - 5 + g(); }\n" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(c23); i += 1)
    {
        char source[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/gnu-c23-%u.c", directory, i);
        const bool written = parse_test_write(source, c23[i].source, strlen(c23[i].source));
        CheckCorpusOracle oracle = { 0 };
        const char* clang_arguments[] = { "-std=c23", "-w" };
        const bool clang_ready = written && check_corpus_clang_file(clang, clang_arguments, BUSTER_ARRAY_LENGTH(clang_arguments), source, &oracle);
        check_test_expect(test, clang_ready && !oracle.rejected, c23[i].name);
        const char* extra[] = { "-std=c23" };
        for (uint32_t route = 0; clang_ready && route < (executable != NULL ? 4u : 2u); route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run_with(route >= 2 ? executable : NULL, source, NULL, (route & 1) != 0, false, true, lazy, extra,
                                                         BUSTER_ARRAY_LENGTH(extra), &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: accepted with no diagnostic", c23[i].name, route);
            check_test_expect(test, ran && !checked.rejected && warnings == 0, description);
        }
        unlink(source);
    }
}

#endif
