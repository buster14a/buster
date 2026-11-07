// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original direct-call return-flow regressions. Expected return-warning counts are independent of the implementation;
// Clang supplies the reference for the ordinary cases and identifies the explicitly retained conservative boundaries.

static uint32_t check_test_return_call_count(const char* text)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, "non-void function does not return a value")) != NULL; at += 1)
    {
        count += 1;
    }
    return count;
}

static void check_test_driver_return_calls(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t warnings;
        uint32_t clang_warnings;
    } cases[] =
    {
        { "ordinary void call", "void g(void); int f(void){g();}\n", 1, 1 },
        { "pragma suppresses direct-call warning", "#pragma clang diagnostic ignored \"-Wreturn-type\"\nvoid g(void); int f(void){g();}\n", 0, 0 },
        { "ordinary int call on the falling path", "int g(int); int f(int x){if(x)return 1;g(x);}\n", 1, 1 },
        { "ordinary calls on both paths", "void g(void); int f(int x){if(x){g();return 1;}g();}\n", 1, 1 },
        { "call followed by another statement", "void g(void); int f(int x){if(x)return 1;g();x++;}\n", 1, 1 },
        { "both branches return after calls", "void g(void); int f(int x){if(x){g();return 1;}g();return 2;}\n", 0, 0 },
        { "unreachable call", "void g(void); int f(void){return 1;g();}\n", 0, 0 },
        { "declared noreturn call", "_Noreturn void die(void); int f(void){die();}\n", 0, 0 },
        { "one branch dies and the other falls", "_Noreturn void die(void); int f(int x){if(x)die();}\n", 1, 1 },
        { "return or noreturn", "_Noreturn void die(void); int f(int x){if(x)return 1;die();}\n", 0, 0 },
        { "noreturn or ordinary call", "_Noreturn void die(void); void g(void); int f(int x){if(x)die();else g();}\n", 1, 1 },
        { "GNU noreturn declaration", "int die(void) __attribute__((noreturn)); int f(void){die();}\n", 0, 0 },
        { "inherited noreturn declaration", "_Noreturn void die(void); void die(void); int f(void){die();}\n", 0, 0 },
        { "parenthesized direct noreturn", "_Noreturn void die(void); int f(void){(&die)();}\n", 0, 0 },
        { "noreturn initializer", "_Noreturn int die(void); int f(void){int n=die();(void)n;}\n", 0, 0 },
        { "noreturn comma operand", "_Noreturn int die(void); int f(void){(void)(die(),1);}\n", 0, 0 },
        { "unevaluated ordinary call", "int g(void); int f(void){(void)sizeof(g());}\n", 1, 1 },
        { "unevaluated noreturn call", "_Noreturn int die(void); int f(void){(void)sizeof(die());}\n", 1, 1 },
        { "short-circuited noreturn call", "_Noreturn int die(void); int f(void){(void)(0&&die());}\n", 1, 1 },
        { "short-circuited or operand", "_Noreturn int die(void); int f(void){(void)(1||die());}\n", 1, 1 },
        { "possibly evaluated noreturn call", "_Noreturn int die(void); int f(int x){(void)(x&&die());}\n", 1, 1 },
        { "both conditional arms die", "_Noreturn int die(void); int f(int x){(void)(x?die():die());}\n", 0, 0 },
        { "one conditional arm returns", "_Noreturn int die(void); int g(void); int f(int x){(void)(x?die():g());}\n", 1, 1 },
        { "selected conditional arm dies", "_Noreturn int die(void); int g(void); int f(void){(void)(1?die():g());}\n", 0, 0 },
        { "noreturn condition", "_Noreturn int die(void); int f(void){if(die())return 1;}\n", 0, 0 },
        { "noreturn argument", "_Noreturn int die(void); void g(int); int f(void){g(die());}\n", 0, 0 },
        { "ordinary builtin operand call", "int g(void); int f(void){(void)__builtin_expect(g(),1);}\n", 1, 1 },
        { "conditional classifier stop remains conservative", "_Noreturn int die(void); int f(double x){(void)__builtin_fpclassify(die(),1,2,3,4,x);}\n", 0, 0 },
        { "terminating builtin", "int f(void){__builtin_trap();}\n", 0, 0 },
        { "unreachable builtin", "int f(void){__builtin_unreachable();}\n", 0, 0 },
        { "builtin stop on one branch", "int f(int x){if(x)__builtin_trap();}\n", 1, 1 },
        { "conservative query contract", "_Noreturn int die(void); int f(void){(void)__builtin_constant_p(die());}\n", 0, 0 },
        { "ordinary indirect call remains conservative", "void (*p)(void); int f(void){p();}\n", 0, 1 },
        { "qualified indirect call remains conservative", "void (*p)(void) __attribute__((noreturn)); int f(void){p();}\n", 0, 0 },
        { "qualified typedef call remains conservative", "typedef void (*NR)(void) __attribute__((noreturn)); NR p; int f(void){p();}\n", 0, 0 },
        { "analyzer noreturn remains conservative", "void die(void) __attribute__((analyzer_noreturn)); int f(void){die();}\n", 0, 0 },
        { "inherited analyzer contract remains conservative", "void die(void) __attribute__((analyzer_noreturn)); void die(void); int f(void){die();}\n", 0, 0 },
    };
    static const char* const flags[] = { NULL, "-Werror=return-type", "-Wno-return-type", "-w" };
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/return-calls.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(flags); policy += 1)
        {
            if (!check_test_driver_takes())
            {
                continue;
            }
            const bool made = parse_test_write(path, cases[c].source, strlen(cases[c].source));
            check_test_expect(test, made, "return-call fixture is written");
            const uint32_t expected = policy >= 2 ? 0 : cases[c].warnings;
            const uint32_t reference = policy >= 2 ? 0 : cases[c].clang_warnings;
            for (uint32_t route = 0; made && route < 4; route += 1)
            {
                char* arguments[14] = { (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                                        "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", path };
                uint32_t used = 5;
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
                char* text = NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                const uint32_t warnings = route == 0 ? reference : expected;
                const int expected_status = policy == 1 && warnings != 0 ? 1 : 0;
                char description[256];
                snprintf(description, sizeof(description), "return-call %s policy=%u route=%u", cases[c].name, policy, route);
                const bool good = status == expected_status && text != NULL && check_test_return_call_count(text) == warnings;
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status %d expected %d; return warnings %u expected %u\n%s\n", status, expected_status,
                            check_test_return_call_count(text), warnings, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    unlink(path);
}
