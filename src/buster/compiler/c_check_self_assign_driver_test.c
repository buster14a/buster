// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original macro-origin self-assignment regressions. Counts are independent expectations, and Clang supplies
// complete diagnostic lines and status. Include after c_check_implicit_driver_test.c's diagnostic filter.

static uint32_t check_test_self_assign_count(const char* text)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, "explicitly assigning value of variable")) != NULL; at += 1)
    {
        count += 1;
    }
    return count;
}

static void check_test_driver_self_assign_macros(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t warnings;
    } cases[] =
    {
        { "direct assignment", "int f(int x){x=x;return x;}\n", 1 },
        { "parenthesized identifiers", "int f(int x){(x)=(x);return x;}\n", 1 },
        { "whole assignment from a macro body", "#define SET(v) do{v=v;}while(0)\nint f(int x){SET(x);return x;}\n", 0 },
        { "operator from a macro", "#define EQ =\nint f(int x){x EQ x;return x;}\n", 0 },
        { "left identifier from a macro body", "#define BODY x\nint f(int x){BODY=x;return x;}\n", 0 },
        { "right identifier from a macro body", "#define BODY x\nint f(int x){x=BODY;return x;}\n", 0 },
        { "left identifier from a macro argument", "#define ID(v) v\nint f(int x){ID(x)=x;return x;}\n", 0 },
        { "right identifier from a macro argument", "#define ID(v) v\nint f(int x){x=ID(x);return x;}\n", 0 },
        { "whole assignment from a macro argument", "#define RUN(v) v\nint f(int x){RUN(x=x);return x;}\n", 0 },
        { "nested assignment macro", "#define INNER(v) v=v\n#define OUTER(v) INNER(v)\nint f(int x){OUTER(x);return x;}\n", 0 },
        { "pasted identifier", "#define CAT(a,b) a##b\nint f(int x){CAT(x,)=x;return x;}\n", 0 },
        { "only opening parenthesis is a macro", "#define OPEN (\nint f(int x){OPEN x)=x;return x;}\n", 1 },
        { "only surrounding parentheses are macros", "#define OPEN (\n#define CLOSE )\nint f(int x){OPEN x CLOSE=OPEN x CLOSE;return x;}\n", 1 },
        { "only declaration is a macro", "#define DECL(v) int v=1\nint f(void){DECL(x);x=x;return x;}\n", 1 },
        { "direct warning after a suppressed macro", "#define SET(v) do{v=v;}while(0)\nint f(int x){SET(x);x=x;return x;}\n", 1 },
        { "volatile variable remains quiet", "int f(volatile int x){x=x;return x;}\n", 0 },
        { "member remains outside plain-variable warning", "struct S{int x;};int f(struct S s){s.x=s.x;return s.x;}\n", 0 },
        { "subscript remains outside plain-variable warning", "int f(void){int a[1]={1};a[0]=a[0];return a[0];}\n", 0 },
        { "indirection remains outside plain-variable warning", "int f(int*p){*p=*p;return *p;}\n", 0 },
        { "explicit cast remains quiet", "int f(int x){x=(int)x;return x;}\n", 0 },
        { "different variables remain quiet", "int f(int x,int y){x=y;return x;}\n", 0 },
        { "source pragma suppresses direct assignment", "#pragma clang diagnostic ignored \"-Wself-assign\"\nint f(int x){x=x;return x;}\n", 0 },
        { "source pragma push and pop preserve direct warning", "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wself-assign\"\nint quiet(int x){x=x;return x;}\n#pragma clang diagnostic pop\nint loud(int x){x=x;return x;}\n", 1 },
    };
    static const char* const flags[] = { NULL, "-Werror=self-assign", "-Wno-self-assign", "-w" };
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/self-assign-macros.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(flags); policy += 1)
        {
            if (!check_test_driver_takes())
            {
                continue;
            }
            const bool written = parse_test_write(path, cases[c].source, strlen(cases[c].source));
            char description[256];
            snprintf(description, sizeof(description), "self-assign %s policy=%u fixture is written", cases[c].name, policy);
            check_test_expect(test, written, description);
            const uint32_t expected = policy >= 2 ? 0 : cases[c].warnings;
            const int expected_status = policy == 1 && expected != 0 ? 1 : 0;
            char reference[4096] = { 0 };
            int oracle = -1;
            bool reference_fits = false;
            for (uint32_t route = 0; written && route < 4; route += 1)
            {
                char* arguments[18] = { (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                                        "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        "-Wall", "-Wextra", path };
                uint32_t used = 8;
                if (flags[policy] != NULL)
                {
                    arguments[used++] = (char*)flags[policy];
                }
                if (route == 2)
                {
                    arguments[used++] = "--scalar";
                }
                else if (route == 3)
                {
                    arguments[used++] = "--deferred-bodies";
                    arguments[used++] = "--lanes";
                    arguments[used++] = "2";
                    arguments[used++] = "--worker-budget";
                    arguments[used++] = "2";
                }
                arguments[used] = NULL;
                char* text = NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                char lines[4096] = { 0 };
                const bool fit = check_test_driver_common_output(text != NULL ? text : "", lines, sizeof(lines));
                if (route == 0)
                {
                    oracle = status;
                    reference_fits = fit;
                    memcpy(reference, lines, strlen(lines) + 1);
                }
                const bool same = route == 0 || (reference_fits && status == oracle && strcmp(reference, lines) == 0);
                const bool good = fit && text != NULL && status == expected_status &&
                                  check_test_self_assign_count(text) == expected && same;
                snprintf(description, sizeof(description), "self-assign %s policy=%u route=%u: independent count, status and Clang diagnostics",
                         cases[c].name, policy, route);
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status %d expected %d; self-assignment diagnostics %u expected %u\nClang lines:\n%s\nActual output:\n%s\n",
                            status, expected_status, check_test_self_assign_count(text), expected, reference, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    unlink(path);
}
