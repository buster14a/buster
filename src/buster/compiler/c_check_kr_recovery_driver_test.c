// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original K&R missing-semicolon cases. A recovered header retains its parameter types, discards malformed tails,
// checks its body and following declarations, and keeps every original parser error. Compare complete diagnostics.
static void check_test_driver_kr_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] =
    {
        "int h(a) int a const; {return a;}\n",
        "int h(a,b) int a int b; {return a+b;}\n",
        "int h(a) int a {return a;}\n",
        "int h(a) int a = 3; {return a;}\n",
        "int h(a,b) int a const; int b volatile; {return a+b;}\n",
        "int h(a) int a const; {return a;}\nint b __attribute__((unknown));\n",
        "int h(a) int *a const; {return *a;}\n",
        "int h(a) int a[3] const; {return a[1];}\n",
        "int h(a) int a = (1+2); {return a;}\n",
        "int h(a) int a const; {return a;}\nint g(b) int b volatile; {return b;}\n",
    };
    static const char* const standards[] = { "-std=gnu89", "-std=gnu17" };
    enum { CAPACITY = 16384 };
    char expected_lines[CAPACITY];
    char actual_lines[CAPACITY];
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/kr-recovery.c", directory);
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const bool written = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        check_test_expect(test, written, "K&R recovery fixture written");
        for (uint32_t s = 0; written && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            char* arguments[] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, CAPACITY);
            check_test_expect(test, lines && oracle == 1, "Clang rejects the original K&R header");
            for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)executable;
                arguments[5] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, CAPACITY) && strcmp(expected_lines, actual_lines) == 0;
                char description[4096];
                snprintf(description, sizeof(description), "K&R recovery %u, %s, path %u: status %d (Clang %d), lines\n%s(Clang\n%s)",
                         f, standards[s], scalar, status, oracle, actual != NULL ? actual_lines : "unavailable", expected_lines);
                check_test_expect(test, status == 1 && same, description);
                free(actual);
            }
            free(expected);
        }
    }
    check_test_expect(test, unlink(source) == 0, "K&R recovery fixture removed");
}
