// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's musttail attribute's errors (#147, c_check_musttail.c) by the driver against clang's, on original fixtures:
// every error, its place and its message, in order, and the exit status, by both paths of -fsyntax-only. test-checker's
// fixtures (c_check_musttail_test.c) compare where each source is first rejected; these tell the messages apart: the
// attribute's spellings ('musttail', 'clang::musttail' and __musttail__'s own where clang's message names its arguments),
// the callee's name where clang has one, its place, the errors of several returns in their order, and a cleanup
// variable's scope's, which clang reports at its function's end, once per scope. Warnings and clang's notes aren't
// compared. Include after c_check_cleanup_driver_test.c, whose error reader this takes.

static void check_test_driver_musttail_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] = {
        { "a direct tail call", "int f(int);\nint g(int x) { __attribute__((musttail)) return f(x); }\n" },
        { "a pointer's tail call", "int (*p)(int);\nint g(int x) { [[clang::musttail]] return p(x); }\n" },
        { "an expression statement's", "int f(int);\nint g(int x) {\n__attribute__((musttail)) f(x);\nreturn 0;\n}\n" },
        { "a null statement's, clang's spelling", "int g(int x) {\n[[clang::musttail]];\nreturn x;\n}\n" },
        { "an if's", "int f(int);\nint g(int x) {\n__attribute__((musttail)) if (x) return f(x);\nreturn 0;\n}\n" },
        { "a declaration's", "int g(int x) {\n__attribute__((musttail)) int y = x;\nreturn y;\n}\n" },
        { "a declaration's, clang's spelling", "int g(int x) {\n[[clang::musttail]] int y = x;\nreturn y;\n}\n" },
        { "a function's", "__attribute__((musttail)) int f(int);\n" },
        { "a parameter's", "void g(int x __attribute__((musttail))) { }\n" },
        { "a member's", "struct S { int a __attribute__((musttail)); };\n" },
        { "with an argument", "int f(int);\nint g(int x) {\n__attribute__((musttail(1))) return f(x);\n}\n" },
        { "__musttail__ with an argument", "int f(int);\nint g(int x) {\n__attribute__((__musttail__(1))) return f(x);\n}\n" },
        { "clang::musttail with an argument", "int f(int);\nint g(int x) {\n[[clang::musttail(1)]] return f(x);\n}\n" },
        { "a return without a value", "void g(void) {\n__attribute__((musttail)) return;\n}\n" },
        { "a value that isn't a call", "int g(int x) {\n__attribute__((musttail)) return x + 1;\n}\n" },
        { "a value that isn't a call, clang's spelling", "int g(int x) {\n[[clang::musttail]] return x;\n}\n" },
        { "a value that isn't a call, __musttail__", "int g(int x) {\n__attribute__((__musttail__)) return x;\n}\n" },
        { "a cast of a call", "long f(int);\nlong g(int x) {\n__attribute__((musttail)) return (long)f(x);\n}\n" },
        { "a conditional of calls", "int f(int);\nint g(int x) {\n__attribute__((musttail)) return x ? f(x) : f(0);\n}\n" },
        { "a comma before a call", "int f(int);\nint g(int x) {\n__attribute__((musttail)) return x, f(x);\n}\n" },
        { "an implicitly converted result", "int f(int);\nlong g(int x) {\n__attribute__((musttail)) return f(x);\n}\n" },
        { "another parameter count", "int f(int, int);\nint g(int x) {\n__attribute__((musttail)) return f(x, x);\n}\n" },
        { "another parameter type", "int f(long);\nint g(int x) {\n  __attribute__((musttail)) return f(x);\n}\n" },
        { "another pointee", "int f(char *);\nint g(int *x) {\n__attribute__((musttail)) return f((char *)x);\n}\n" },
        { "a pointer's other signature", "int (*p)(long);\nint g(int x) {\n__attribute__((musttail)) return p(x);\n}\n" },
        { "a member's other signature", "struct S { int (*p)(int); };\nint g(int x, struct S *s) {\n__attribute__((musttail)) return s->p(x);\n}\n" },
        { "a table's other signature, unnamed", "int (*table[4])(int, int);\nint g(int x) {\n__attribute__((musttail)) return table[x & 3](x, x);\n}\n" },
        { "a conditional callee's other signature, unnamed", "int f1(int, int);\nint f2(int, int);\nint g(int x) {\n"
                                                              "__attribute__((musttail)) return (x ? f1 : f2)(x, x);\n}\n" },
        { "a variadic callee", "int f(int, ...);\nint g(int x) {\n__attribute__((musttail)) return f(x);\n}\n" },
        { "a variadic caller, clang's spelling", "int f(int);\nint g(int x, ...) {\n[[clang::musttail]] return f(x);\n}\n" },
        { "a caller without a prototype", "int f(void);\nint g() {\n__attribute__((musttail)) return f();\n}\n" },
        { "another calling convention", "__attribute__((ms_abi)) int f(int);\nint g(int x) {\n__attribute__((musttail)) return f(x);\n}\n" },
        { "a caller of another calling convention", "int f(int);\n__attribute__((ms_abi)) int g(int x) {\n__attribute__((musttail)) return f(x);\n}\n" },
        { "a [[noreturn]] callee", "[[noreturn]] int f(int);\nint g(int x) {\n__attribute__((musttail)) return f(x);\n}\n" },
        { "two attributes, another signature", "int f(long);\nint g(int x) {\n__attribute__((musttail)) __attribute__((musttail)) return f(x);\n}\n" },
        { "errors of two returns, in order", "int f(long);\nint g(int x) {\n__attribute__((musttail)) return f(x);\n}\n"
                                             "int h(int x) {\n__attribute__((musttail)) return x;\n}\n" },
        { "a cleanup's scope", "void c(int *);\nint f(int);\nint g(int x) {\nint y __attribute__((cleanup(c))) = x;\n__attribute__((musttail)) return f(x);\n}\n" },
        { "two cleanups' scopes", "void c(int *);\nint f(int);\nint g(int x) {\nint y __attribute__((cleanup(c))) = x;\n{\nint z __attribute__((cleanup(c))) = x;\n"
                                  "[[clang::musttail]] return f(x);\n}\n}\n" },
        { "a for statement's cleanup's scope", "void c(int *);\nint f(int);\nint g(int x) {\nfor (int y __attribute__((cleanup(c))) = x; y; ) {\n"
                                               "__attribute__((musttail)) return f(x);\n}\nreturn 0;\n}\n" },
        { "a statement expression's cleanup's scope", "void c(int *);\nint f(int);\nint g(int x) {\nreturn ({ int y __attribute__((cleanup(c))) = x;\n"
                                                      "__attribute__((musttail)) return f(x);\n0; });\n}\n" },
        // A scope's error is its function's end's, after a later return's own error in the function.
        { "a scope's error after a later return's", "void c(int *);\nint f(int);\nint g(int x) {\nif (x) {\nint y __attribute__((cleanup(c))) = x;\n"
                                                    "__attribute__((musttail)) return f(x);\n}\n__attribute__((musttail)) return x;\n}\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/musttail-errors.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "musttail errors, %s: clang runs it", fixtures[i].name);
        check_test_expect(test, made && (oracle == 0 || oracle == 1) && expected_valid && (oracle == 0) == (expected[0] == 0), description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "musttail errors, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
}
