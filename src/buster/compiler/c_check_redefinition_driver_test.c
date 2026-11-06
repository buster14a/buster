// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compare complete redefinition diagnostics, including the previous declaration/definition notes. Every input is
// rejected; source-order and deferred body schedules must retain the same diagnostic metadata.
static void check_test_driver_redefinitions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        "double x; int x;\n",
        "int f(void){return 1;} int f(void){return 2;}\n",
        "int f(void); short f(void);\n",
        "int a=1; int a=2;\n",
        "void f(int x, int x);\n",
        "int a; static int a;\n",
        "static int a; int a;\n",
        "static int a = 1; int a;\n",
        "static int a=1; extern int a; int a;\n",
        "int a=1; extern int a; static int a;\n",
        "static int a; double a;\n",
        "extern int a; static double a;\n",
        "int f(void); static double f(void);\n",
        "static int f(void); double f(void);\n",
        "extern int a; double a;\n",
        "int a=1; extern int a; int a=2;\n",
        "typedef double D; D a; int a;\n",
        "void f(void){int a; int a;}\n",
        "void f(void){int a; double a;}\n",
        "int f(void); static int f(void);\n",
        "#define NAME a\nint NAME=1; int NAME=2;\n",
        "void f(int x, int y, int x);\n",
    };
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/redefinition.c", directory);
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        const bool written = parse_test_write(path, fixtures[f], strlen(fixtures[f]));
        char* reference[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", path, NULL };
        char* expected = NULL;
        const int oracle = written ? check_test_driver_spawn(reference, &expected) : -1;
        check_test_expect(test, written && oracle == 1 && expected != NULL, "redefinition oracle rejects original source");
        bool printed = false;
        for (uint32_t schedule = 0; expected != NULL && schedule < 2; schedule += 1)
        {
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* arguments[] = { (char*)executable, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", path,
                                      "--lanes", schedule != 0 ? "3" : "1", scalar != 0 ? "--scalar" : NULL, NULL };
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = status == oracle && actual != NULL && strcmp(actual, expected) == 0;
                char description[128];
                snprintf(description, sizeof(description), "redefinition %u schedule %u path %u: complete Clang diagnostics", f, schedule, scalar);
                check_test_expect(test, same, description);
                if (!same && !printed)
                {
                    fprintf(stderr, "%s: status %d (Clang %d)\nBuster:\n%s\nClang:\n%s\n", description, status, oracle,
                            actual != NULL ? actual : "<no output>", expected);
                    printed = true;
                }
                free(actual);
            }
        }
        free(expected);
    }
    check_test_expect(test, unlink(path) == 0, "redefinition fixture removed");
}
