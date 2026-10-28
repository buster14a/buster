// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's cleanup attribute's errors (#147) by the driver against clang's, on original fixtures: every error, its place and
// its message, in order, by both paths of -fsyntax-only. test-checker's fixtures (c_check_cleanup_test.c) compare where
// each source is first rejected; these tell the messages apart. Two differences are taken: clang names the two types of
// its parameter's error, where the driver says what clang's other spelling of the message does ("parameter has
// incompatible type"); and after a syntax error in a declaration (a type's name as the argument), clang's later errors
// in it, which the parser skips with the rest of the declaration. A cleanup's error, as the reader's errors but an
// undeclared name's, ends the unit's checking (check_input_diagnostic_recover), so each fixture has one, or undeclared
// names. The notes clang attaches to the jumps' errors aren't compared. Include after c_checker_driver_recovery_test.c.

// The errors of a diagnostic text, each "file:line:column: error: message" and a newline, at most `limit` of them (all for
// 0), with clang's "parameter has type 'P' which is incompatible with type 'V'" as "parameter has incompatible type".
static bool check_test_cleanup_errors(const char* text, uint32_t limit, char* errors, uint64_t capacity)
{
    static const char typed[] = " parameter has type '";
    static const char untyped[] = " parameter has incompatible type";
    uint64_t used = 0;
    uint32_t taken = 0;
    bool valid = text != NULL && capacity != 0;
    for (const char* at = text; valid && at != NULL && *at != 0 && (limit == 0 || taken < limit);)
    {
        const char* end = strchr(at, '\n');
        const uint64_t length = end != NULL ? (uint64_t)(end - at) : strlen(at);
        if (memmem(at, length, ": error: ", 9) != NULL)
        {
            const char* type = memmem(at, length, typed, sizeof(typed) - 1);
            const uint64_t kept = type != NULL ? (uint64_t)(type - at) : length;
            const uint64_t added = type != NULL ? sizeof(untyped) - 1 : 0;
            valid = kept + added + 1 < capacity - used;
            if (valid)
            {
                memcpy(errors + used, at, kept);
                memcpy(errors + used + kept, untyped, added);
                used += kept + added;
                errors[used++] = '\n';
            }
            taken += 1;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    if (capacity != 0)
    {
        errors[used] = 0;
    }
    return valid;
}

static void check_test_driver_cleanup_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        // The errors compared, all for 0.
        uint32_t limit;
    } fixtures[] = {
        { "an undeclared function", "void g(void) {\nint a __attribute__((cleanup(missing)));\n}\n", 0 },
        { "an undeclared name on a file-scope variable", "int a\n__attribute__((cleanup(missing)));\n", 0 },
        { "an undeclared name on an ignored parameter", "void g(int a __attribute__((cleanup(missing)))) { }\n", 0 },
        { "an undeclared name on an ignored field", "struct S { int a __attribute__((cleanup(missing))); };\n", 0 },
        { "an earlier cleanup's undeclared name", "void f(int *);\nvoid g(void) { int a __attribute__((cleanup(missing), cleanup(f))); }\n", 0 },
        { "an undeclared name in parentheses", "void g(void) {\nint a __attribute__((cleanup((missing))));\n}\n", 0 },
        { "its own variable", "void g(void) {\nint a __attribute__((cleanup(a)));\n}\n", 0 },
        { "an object", "int f;\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function pointer", "void f(int *p);\nvoid (*h)(int *) = f;\nvoid g(void) {\nint a __attribute__((cleanup(h)));\n}\n", 0 },
        { "an enumerator", "enum { E };\nvoid g(void) {\nint a __attribute__((cleanup(E)));\n}\n", 0 },
        { "a function in parentheses", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup((f))));\n}\n", 0 },
        { "an object in two parentheses", "int o;\nvoid g(void) {\nint a __attribute__((cleanup(((o)))));\n}\n", 0 },
        { "a string", "void g(void) {\nint a __attribute__((cleanup(\"f\")));\n}\n", 0 },
        { "a number", "void g(void) {\nint a __attribute__((cleanup(1)));\n}\n", 0 },
        { "a character", "void g(void) {\nint a __attribute__((cleanup('f')));\n}\n", 0 },
        { "a literal on a file-scope variable, ignored", "int a __attribute__((cleanup(1)));\nvoid g(void) {\nint b __attribute__((cleanup(b)));\n}\n", 0 },
        { "a type, a syntax error", "typedef int T;\nvoid g(void) {\nint a __attribute__((cleanup(T)));\n}\n", 1 },
        { "an earlier cleanup's type, a syntax error", "typedef int T;\nvoid f(int *);\nvoid g(void) { int a __attribute__((cleanup(T), cleanup(f))); }\n", 1 },
        { "no argument", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup));\n}\n", 0 },
        { "empty parentheses", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup()));\n}\n", 0 },
        { "two arguments", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(f, f)));\n}\n", 0 },
        { "two arguments on a file-scope variable, ignored", "void f(int *p);\nint a __attribute__((cleanup(f, f)));\nvoid g(void) {\nint b __attribute__((cleanup()));\n}\n", 0 },
        { "two arguments before one", "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(f, f), cleanup(f)));\n}\n", 0 },
        { "a function without a prototype", "void f();\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function of two parameters", "void f(int *p, int q);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a function of no parameter", "void f(void);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "another pointee", "void f(long *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "another signedness", "void f(int *p);\nvoid g(void) {\nunsigned a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a character pointer", "void f(char *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "a pointer to a pointer to void", "void f(void **p);\nvoid g(void) {\nint *a __attribute__((cleanup(f)));\n}\n", 0 },
        { "an integer parameter", "void f(int p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", 0 },
        { "an element's pointer", "void f(int *p);\nvoid g(int n) {\nint a[n] __attribute__((cleanup(f)));\n}\n", 0 },
        { "another struct", "struct S { int x; };\nstruct T { int x; };\nvoid f(struct T *p);\nvoid g(void) {\nstruct S s __attribute__((cleanup(f)));\n}\n", 0 },
        { "undeclared names of several declarations", "void g(void) {\nint a __attribute__((cleanup(missing)));\nint b __attribute__((cleanup(other)));\n}\n"
                                                      "void h(void) {\nint c __attribute__((cleanup((gone))));\n}\n", 0 },
        { "a goto into its scope", "void f(int *p);\nvoid g(int n) {\ngoto l;\nint a __attribute__((cleanup(f)));\nl:;\n}\n", 0 },
        { "a goto into a nested scope", "void f(int *p);\nvoid g(int n) {\ngoto l;\n{ int a __attribute__((cleanup(f))); l:; }\n}\n", 0 },
        { "a switch into its scope", "void f(int *p);\nvoid g(int n) { switch (n) {\nint a __attribute__((cleanup(f)));\ncase 1:;\n} }\n", 0 },
        { "a computed goto out of its scope", "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\ngoto *t[0]; }\nl:;\n}\n", 0 },
        { "a computed goto into its scope", "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\nl:; }\ngoto *t[0];\n}\n", 0 },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/cleanup-errors.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, fixtures[i].limit, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "cleanup errors, %s: clang rejects it", fixtures[i].name);
        check_test_expect(test, made && oracle == 1 && expected_valid && expected[0] != 0, description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, fixtures[i].limit, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "cleanup errors, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
}
