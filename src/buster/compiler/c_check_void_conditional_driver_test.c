// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original explicit-void conditional regressions with independent diagnostic counts and Clang line comparisons.
// Include after c_check_implicit_driver_test.c's shared diagnostic filter.

static uint32_t check_test_void_conditional_count(const char* text, const char* needle)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, needle)) != NULL; at += 1)
    {
        count += 1;
    }
    return count;
}

static void check_test_driver_void_conditionals(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t unused;
        uint32_t other_warnings;
    } cases[] =
    {
        { "reported void decrement conditional", "struct F{int pos;};void f(struct F*p,int c){c?(void)p->pos--:(void)0;}\n", 0, 0 },
        { "both pure void casts", "void f(int c,int x,int y){c?(void)x:(void)y;}\n", 0, 0 },
        { "void typedef cast", "typedef void V;void f(int c,int x){c?(V)x:(V)0;}\n", 0, 0 },
        { "parenthesized void casts", "void f(int c,int x){(c?((void)x):((void)0));}\n", 0, 0 },
        { "void calls", "void a(void);void b(void);void f(int c){c?a():b();}\n", 0, 0 },
        { "void cast and call", "void a(void);void f(int c,int x){c?(void)x:a();}\n", 0, 0 },
        { "nested void conditionals", "void f(int c,int d,int x){c?(d?(void)x:(void)0):(void)0;}\n", 0, 0 },
        { "extension wrappers retain void discard", "void f(int c,int x){c?__extension__((void)x):__extension__((void)0);}\n", 0, 0 },
        { "generic selection retains void discard", "void f(int c,int x){c?_Generic(x,int:(void)x):_Generic(x,int:(void)0);}\n", 0, 0 },
        { "chosen expression retains void discard", "void f(int c,int x){c?__builtin_choose_expr(1,(void)x,x):__builtin_choose_expr(0,x,(void)0);}\n", 0, 0 },
        { "extension scalar result still warns", "void f(int c,int x,int y){c?__extension__(x):y;}\n", 1, 0 },
        { "generic scalar result still warns", "void f(int c,int x,int y){c?_Generic(x,int:x):y;}\n", 1, 0 },
        { "unchosen void cast cannot silence scalar result", "void f(int c,int x,int y){c?__builtin_choose_expr(1,x,(void)0):y;}\n", 1, 0 },
        { "void dereference result is not explicit discard", "#pragma clang diagnostic ignored \"-Wvoid-ptr-dereference\"\nvoid f(int c,void*p){c?*p:*p;}\n", 1, 0 },
        { "extension retains inner unused comma", "void f(int c){c?__extension__((1,(void)0)):__extension__((void)0);}\n", 1, 0 },
        { "extension retains inner division warning", "void f(int c,int x){c?__extension__((void)(x/0)):__extension__((void)0);}\n", 0, 1 },
        { "void cast on comma result path", "void sink(void);void f(int c,int x){c?(sink(),(void)x):(void)0;}\n", 0, 0 },
        { "void casts in both comma operands", "void f(int c,int x,int y){c?((void)x,(void)y):(void)0;}\n", 0, 0 },
        { "assignment before void comma result", "void f(int c,int x,int y){c?(x=1,(void)y):(void)0;}\n", 0, 0 },
        { "void conditional as left comma operand", "void sink(void);void f(int c,int x){(c?(void)x:(void)0),sink();}\n", 0, 0 },
        { "void conditional as right comma operand", "void sink(void);void f(int c,int x){sink(),(c?(void)x:(void)0);}\n", 0, 0 },
        { "void conditionals in for clauses", "void f(int c,int x){for(c?(void)x:(void)0;c;c?(void)x:(void)0){c=0;}}\n", 0, 0 },
        { "reported macro form", "#define UNGET(p,c) ((c)?(void)(p)->pos--:(void)0)\nstruct F{int pos;};void f(struct F*p,int c){UNGET(p,c);}\n", 0, 0 },
        { "scalar conditional still warns", "void f(int c,int x,int y){c?x:y;}\n", 1, 0 },
        { "nonvoid cast still warns", "void f(int c,int x,int y){c?(int)x:y;}\n", 1, 0 },
        { "effect away from comma result still warns", "void sink(void);void f(int c,int x,int y){c?(sink(),x):y;}\n", 1, 0 },
        { "assignment arm still quiet", "void f(int c,int x,int y){c?(x=1):y;}\n", 0, 0 },
        { "inner unused comma remains diagnosed", "void f(int c){c?(1,(void)0):(void)0;}\n", 1, 0 },
        { "inner division warning remains diagnosed", "void f(int c,int x){c?(void)(x/0):(void)0;}\n", 0, 1 },
        { "source pragma suppresses scalar control", "#pragma clang diagnostic ignored \"-Wunused-value\"\nvoid f(int c,int x,int y){c?x:y;}\n", 0, 0 },
        { "source pragma push and pop preserve scalar control", "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wunused-value\"\nvoid quiet(int c,int x,int y){c?x:y;}\n#pragma clang diagnostic pop\nvoid loud(int c,int x,int y){c?x:y;}\n", 1, 0 },
    };
    static const char* const flags[] = { NULL, "-Werror=unused-value", "-Wno-unused-value", "-w" };
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/void-conditionals.c", directory);
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
            snprintf(description, sizeof(description), "void conditional %s policy=%u fixture is written", cases[c].name, policy);
            check_test_expect(test, written, description);
            const uint32_t unused = policy >= 2 ? 0 : cases[c].unused;
            const uint32_t other = policy == 3 ? 0 : cases[c].other_warnings;
            const uint32_t errors = policy == 1 ? unused : 0;
            const uint32_t warnings = policy == 1 ? other : unused + other;
            const int expected_status = errors != 0 ? 1 : 0;
            char reference[4096] = { 0 };
            int oracle = -1;
            bool reference_fits = false;
            for (uint32_t route = 0; written && route < 4; route += 1)
            {
                char* arguments[18] = { (char*)(route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe"),
                                        "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        "-Wunused-value", path };
                uint32_t used = 7;
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
                const uint32_t actual_unused = check_test_void_conditional_count(text, "expression result unused") +
                                               check_test_void_conditional_count(text, "left operand of comma operator has no effect");
                const bool same = route == 0 || (reference_fits && status == oracle && strcmp(reference, lines) == 0);
                const bool good = fit && text != NULL && status == expected_status && actual_unused == unused &&
                                  check_test_void_conditional_count(text, ": warning: ") == warnings &&
                                  check_test_void_conditional_count(text, ": error: ") == errors && same;
                snprintf(description, sizeof(description), "void conditional %s policy=%u route=%u: independent counts, status and Clang diagnostics",
                         cases[c].name, policy, route);
                check_test_expect(test, good, description);
                if (!good)
                {
                    fprintf(stderr, "  status %d expected %d; unused %u expected %u; warnings %u expected %u; errors %u expected %u\nClang lines:\n%s\nActual output:\n%s\n",
                            status, expected_status, actual_unused, unused, check_test_void_conditional_count(text, ": warning: "), warnings,
                            check_test_void_conditional_count(text, ": error: "), errors, reference, text != NULL ? text : "(missing)");
                }
                free(text);
            }
        }
    }
    unlink(path);
}
