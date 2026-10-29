// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #147: GNU C's local labels and __auto_type against clang, through the driver: each fixture's first error at clang's
// line and column, and with clang's text where the driver's message is clang's; and where a unit has several label
// errors, every place's, whatever their order. The driver gives a place one error (`firsts`), its first: clang's second
// at a use of an ambiguous name, its undeclared label, is left out.
#ifndef BUSTER_C_CHECKER_DRIVER_GNU_TEST_INCLUDED
#define BUSTER_C_CHECKER_DRIVER_GNU_TEST_INCLUDED

// The error lines of `text` (`file:line:column: error: message`), each place's first, sorted, into `errors` with a
// newline after each: the places' errors whatever their order. Returns false for more lines than it keeps or than
// `errors` has room for.
static bool check_test_driver_gnu_errors(char* text, char* errors, size_t capacity)
{
    char* lines[32];
    uint32_t count = 0;
    bool valid = text != NULL && capacity != 0;
    for (char* at = text; valid && at != NULL && *at != 0;)
    {
        char* end = strchr(at, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        const char* error = strstr(at, ": error: ");
        bool repeated = false;
        for (uint32_t i = 0; error != NULL && i < count && !repeated; i += 1)
        {
            repeated = strncmp(lines[i], at, (size_t)(error - at) + 9) == 0;
        }
        if (error != NULL && !repeated)
        {
            valid = count < BUSTER_ARRAY_LENGTH(lines);
            lines[valid ? count : 0] = at;
            count += valid;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    for (uint32_t i = 1; valid && i < count; i += 1)
    {
        char* line = lines[i];
        uint32_t j = i;
        while (j != 0 && strcmp(lines[j - 1], line) > 0)
        {
            lines[j] = lines[j - 1];
            j -= 1;
        }
        lines[j] = line;
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
    // use), and an undeclared label at each declaration; with a name they declare once and its block doesn't define.
    static const struct
    {
        const char* name;
        const char* source;
    } every[] =
    {
        { "a name one list declares twice, defined", "void f(void) {\n    {\n        __label__ a, a;\n    a:;\n    }\n}\n" },
        { "a name two lists declare twice, a goto's", "void f(void) {\n    {\n        __label__ a;\n        __label__ a;\n        goto a;\n    }\n}\n" },
        { "a name declared twice, an address's", "void f(void) {\n    {\n        __label__ a, a;\n        void *p = &&a;\n        (void)p;\n    }\n}\n" },
        { "a name declared twice, used and defined", "void f(void) {\n    {\n        __label__ a, a;\n        goto a;\n    a:;\n    }\n}\n" },
        { "a name declared twice, never used", "void f(void) {\n    {\n        __label__ a, a;\n    }\n}\n" },
        { "a list's two names, one undefined", "void f(void) {\n    {\n        __label__ a, b;\n    b:;\n    }\n}\n" },
    };
    for (uint32_t i = 0; executable != NULL && i < BUSTER_ARRAY_LENGTH(every); i += 1)
    {
        char source[192];
        char description[1024];
        snprintf(source, sizeof(source), "%s/gnu-every-%u.c", directory, i);
        const bool written = parse_test_write(source, every[i].source, strlen(every[i].source));
        char* const clang_arguments[] = { (char*)clang, "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
        char* text = NULL;
        char expected[512];
        const bool oracle = written && check_test_driver_spawn(clang_arguments, &text) == 1 && check_test_driver_gnu_errors(text, expected, sizeof(expected));
        free(text);
        check_test_expect(test, oracle && expected[0] != 0, every[i].name);
        for (uint32_t scalar = 0; oracle && scalar < 2; scalar += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-fsyntax-only", "-std=gnu17", "-w", source, NULL };
            char errors[512];
            const bool ran = check_test_driver_spawn(scalar != 0 ? scalar_arguments : vector_arguments, &text) == 1 &&
                             check_test_driver_gnu_errors(text, errors, sizeof(errors));
            free(text);
            snprintf(description, sizeof(description), "%s, %s path: every error clang's:\n%s(ours)\n%s", every[i].name, scalar != 0 ? "scalar" : "vector",
                     ran ? errors : "(none)\n", expected);
            check_test_expect(test, ran && strcmp(errors, expected) == 0, description);
        }
        unlink(source);
    }
}

#endif
