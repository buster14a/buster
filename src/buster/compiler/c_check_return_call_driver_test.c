// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source-call return-flow regressions. Expected return-warning counts are independent of the implementation;
// Clang supplies the reference through direct, indirect, overloaded and builtin calls and source-visible annotations.

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
        { "later translation-unit noreturn does not change an earlier body", "void die(void); int f(void){die();} _Noreturn void die(void);\n", 1, 1 },
        { "later block noreturn does not change an earlier call", "void die(void); int f(void){die(); _Noreturn void die(void);}\n", 1, 1 },
        { "block noreturn applies to a later call", "void die(void); int f(void){die(); _Noreturn void die(void); die();}\n", 0, 0 },
        { "parenthesized direct noreturn", "_Noreturn void die(void); int f(void){(&die)();}\n", 0, 0 },
        { "noreturn initializer", "_Noreturn int die(void); int f(void){int n=die();(void)n;}\n", 0, 0 },
        { "ordinary direct initializer", "int g(void); int f(void){int n=g();(void)n;}\n", 1, 1 },
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
        { "classifier source CFG keeps a selector's stop", "_Noreturn int die(void); int f(double x){(void)__builtin_fpclassify(die(),1,2,3,4,x);}\n", 0, 0 },
        { "terminating builtin", "int f(void){__builtin_trap();}\n", 0, 0 },
        { "unreachable builtin", "int f(void){__builtin_unreachable();}\n", 0, 0 },
        { "builtin stop on one branch", "int f(int x){if(x)__builtin_trap();}\n", 1, 1 },
        { "constant_p source CFG visits its operand", "_Noreturn int die(void); int f(void){(void)__builtin_constant_p(die());}\n", 0, 0 },
        { "ordinary indirect call", "void (*p)(void); int f(void){p();}\n", 1, 1 },
        { "ordinary indirect initializer", "int (*p)(void); int f(void){int n=p();(void)n;}\n", 1, 1 },
        { "qualified indirect initializer", "int (*p)(void) __attribute__((noreturn)); int f(void){int n=p();(void)n;}\n", 0, 0 },
        { "qualified typedef initializer", "typedef int (*NR)(void) __attribute__((noreturn)); NR p; int f(void){int n=p();(void)n;}\n", 0, 0 },
        { "qualified indirect call", "void (*p)(void) __attribute__((noreturn)); int f(void){p();}\n", 0, 0 },
        { "qualified typedef call", "typedef void (*NR)(void) __attribute__((noreturn)); NR p; int f(void){p();}\n", 0, 0 },
        { "analyzer noreturn", "void die(void) __attribute__((analyzer_noreturn)); int f(void){die();}\n", 0, 0 },
        { "inherited analyzer contract", "void die(void) __attribute__((analyzer_noreturn)); void die(void); int f(void){die();}\n", 0, 0 },
        { "wrapped analyzer contract", "void die(void) __attribute__((__analyzer_noreturn__)); int f(void){die();}\n", 0, 0 },
        { "inherited wrapped analyzer", "void die(void) __attribute__((__analyzer_noreturn__)); void die(void); int f(void){die();}\n", 0, 0 },
        { "indirect stop leaves the other branch", "void (*p)(void) __attribute__((noreturn)); int f(int x){if(x)p();}\n", 1, 1 },
        { "typedef stop leaves the other branch", "typedef void (*NR)(void) __attribute__((noreturn)); NR p; int f(int x){if(x)p();}\n", 1, 1 },
        { "indirect stop and ordinary indirect call", "void (*p)(void) __attribute__((noreturn)); void (*g)(void); int f(int x){if(x)p();else g();}\n", 1, 1 },
        { "ordinary pointer member", "struct S{void(*p)(void);};int f(struct S*s){s->p();}\n", 1, 1 },
        { "noreturn pointer member", "typedef void(*NR)(void)__attribute__((noreturn));struct S{NR p;};int f(struct S*s){s->p();}\n", 0, 0 },
        { "ordinary pointer array", "void(*p[2])(void);int f(int n){p[n&1]();}\n", 1, 1 },
        { "noreturn pointer array", "typedef void(*NR)(void)__attribute__((noreturn));NR p[2];int f(int n){p[n&1]();}\n", 0, 0 },
        { "returned noreturn pointer", "typedef void(*NR)(void)__attribute__((noreturn));NR g(void);int f(void){g()();}\n", 0, 0 },
        { "typeof GNU function retains type noreturn", "void g(void)__attribute__((noreturn));typeof(g)*p;int f(void){p();}\n", 0, 0 },
        { "typeof address retains GNU noreturn", "void g(void)__attribute__((noreturn));typeof(&g)p;int f(void){p();}\n", 0, 0 },
        { "typeof keyword function has ordinary callable type", "_Noreturn void g(void);typeof(g)*p;int f(void){p();}\n", 1, 1 },
        { "typeof keyword address has ordinary callable type", "_Noreturn void g(void);typeof(&g)p;int f(void){p();}\n", 1, 1 },
        { "explicit cast drops noreturn type", "void(*p)(void)__attribute__((noreturn));int f(void){((void(*)(void))p)();}\n", 1, 1 },
        { "explicit cast establishes noreturn type", "typedef void(*NR)(void)__attribute__((noreturn));void(*p)(void);int f(void){((NR)p)();}\n", 0, 0 },
        { "mixed conditional has ordinary callable type", "void(*p)(void)__attribute__((noreturn));void(*g)(void);int f(int n){(n?p:g)();}\n", 1, 1 },
        { "null conditional preserves noreturn callable type", "void(*p)(void)__attribute__((noreturn));int f(int n){(n?p:0)();}\n", 0, 0 },
        { "both conditional callable types are noreturn", "typedef void(*NR)(void)__attribute__((noreturn));NR p,g;int f(int n){(n?p:g)();}\n", 0, 0 },
        { "comma has its right callable type", "void(*p)(void)__attribute__((noreturn));void(*g)(void);int f(void){(p,g)();}\n", 1, 1 },
        { "comma preserves right noreturn callable type", "void(*p)(void)__attribute__((noreturn));void(*g)(void);int f(void){(g,p)();}\n", 0, 0 },
        { "ordinary constant query", "int f(void){(void)__builtin_constant_p(1);}\n", 1, 1 },
        { "ordinary call inside constant query", "int g(void);int f(void){(void)__builtin_constant_p(g());}\n", 1, 1 },
        { "query stop on one branch", "_Noreturn int die(void);int f(int n){if(n)(void)__builtin_constant_p(die());}\n", 1, 1 },
        { "object-size query returns", "int f(void*p){(void)__builtin_object_size(p,0);}\n", 1, 1 },
        { "object-size query excludes a noreturn operand", "_Noreturn void*die(void);int f(void){(void)__builtin_object_size(die(),0);}\n", 1, 1 },
        { "dynamic object-size query excludes a noreturn operand", "_Noreturn void*die(void);int f(void){(void)__builtin_dynamic_object_size(die(),0);}\n", 1, 1 },
        { "classifier ordinary call returns", "int g(void);int f(double d){(void)__builtin_fpclassify(g(),1,2,3,4,d);}\n", 1, 1 },
        { "classifier stop on one branch", "_Noreturn int die(void);int f(double d,int n){if(n)(void)__builtin_fpclassify(die(),1,2,3,4,d);}\n", 1, 1 },
        { "analyzer stop leaves the other branch", "void die(void)__attribute__((analyzer_noreturn));int f(int n){if(n)die();}\n", 1, 1 },
        { "ordinary overload selected", "void g(int)__attribute__((overloadable));void g(double)__attribute__((overloadable,noreturn));int f(void){g(1);}\n", 1, 1 },
        { "noreturn overload selected", "void g(int)__attribute__((overloadable));void g(double)__attribute__((overloadable,noreturn));int f(void){g(1.0);}\n", 0, 0 },
        { "library abort contract", "void abort(void);int f(void){abort();}\n", 0, 0 },
        { "library exit contract", "void exit(int);int f(void){exit(1);}\n", 0, 0 },
        { "library stop leaves the other branch", "void abort(void);int f(int n){if(n)abort();}\n", 1, 1 },
        { "quick_exit needs its explicit contract", "void quick_exit(int);int f(void){quick_exit(1);}\n", 1, 1 },
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
