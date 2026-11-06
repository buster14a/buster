// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Null declarations containing attributes and language-mode-sensitive qualifier spellings. Both source paths compare
// complete diagnostics with the external Clang oracle; valid C89 declarations also exercise typedefs and declarators.
static void check_test_driver_empty_declarations(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* source;
    } fixtures[] =
    {
        { "-std=c89", "restrict;\n" },
        { "-std=c89", "int restrict; int f(void){return restrict;}\n" },
        { "-std=c89", "int *restrict;\n" },
        { "-std=c89", "typedef int restrict; restrict x;\n" },
        { "-std=c89", "typedef int restrict; int f(void){return sizeof(restrict);}\n" },
        { "-std=c89", "int f(int *restrict){return restrict!=0;}\n" },
        { "-std=c89", "struct S { int restrict; }; int f(struct S *p){return p->restrict;}\n" },
        { "-std=gnu17", "__attribute__((unused));\n" },
        { "-std=gnu17", "__attribute__(()); int x;\n" },
        { "-std=gnu17", "__attribute__((unused)) const;\n" },
        { "-std=gnu17", "__extension__ __attribute__((unused));\n" },
        { "-std=c23", "[[maybe_unused]];\n" },
        { "-std=c23", "[[gnu::unused]];\n" },
        { "-std=c23", "[[deprecated]];\n" },
        { "-std=c23", "[[maybe_unused]] [[deprecated]]; int x;\n" },
        { "-std=c23", "[[unknown]];\n" },
        { "-std=c23", "[[maybe_unused]] const;\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/empty-declaration.c", directory);
    enum { CAPACITY = 16384 };
    char expected_lines[CAPACITY];
    char actual_lines[CAPACITY];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const bool written = parse_test_write(source, fixtures[f].source, strlen(fixtures[f].source));
        char* arguments[] = { (char*)clang, (char*)fixtures[f].standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, CAPACITY);
        check_test_expect(test, written && lines && oracle >= 0, "empty declaration fixture and oracle available");
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, CAPACITY) && strcmp(expected_lines, actual_lines) == 0;
            char description[4096];
            snprintf(description, sizeof(description), "empty declaration %u, path %u: status %d (Clang %d), lines\n%s(Clang\n%s)",
                     f, scalar, status, oracle, actual != NULL ? actual_lines : "unavailable", expected_lines);
            check_test_expect(test, status >= 0 && same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    check_test_expect(test, unlink(source) == 0, "empty declaration fixture removed");
}
