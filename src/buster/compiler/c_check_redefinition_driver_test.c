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
        // An extern declaration's type conflict is clang's "redeclaration", in a block and at file scope (#1447).
        "int x;\nvoid f(void) { extern double x; }\n",
        "void f(void) { extern int y; }\nvoid g(void) { extern double y; }\n",
        "int a;\nextern double a;\n",
        "extern int b;\nextern double b;\n",
        // Its note is the entity's latest earlier declaration, another block's out of scope.
        "int y;\nvoid f(void) { extern int y; }\nvoid g(void) { extern double y; }\n",
        "int v;\nvoid f(void) { extern int v; }\ndouble v;\n",
        // A function's is the declaration its lookup finds, never another block's (review on #1453).
        "int fn(int);\nvoid f(void) { extern int fn(int); }\nvoid g(void) { extern double fn(int); }\n",
        "int fn(int);\nvoid f(void) { extern int fn(int); }\ndouble fn(int);\n",
        "int fn(int);\nint fn(int);\ndouble fn(int);\n",
        "int fn(int);\nvoid f(void) { extern int fn(int); extern double fn(int); }\n",
        // Extern is read from the specifiers, whatever its declarator's chunks or its specifiers' spelling.
        "extern int a[];\nextern char a[];\n",
        "int b[3];\nextern int b[5];\n",
        "int q[2];\nextern int q[2][3];\n",
        "int a[2];\nvoid f(void) { extern int a[3]; }\n",
        "int *p[2];\nextern int (*p)[2];\n",
        "int a3[] = {1};\nextern char a3[];\n",
        "extern int z, a2[];\nextern int z, a2[2];\nextern char a2[];\n",
        "struct S { int x; } c;\nextern struct S2 { int y; } c;\n",
        "union U { int i; } u;\nextern union U2 { int j; } u;\n",
        "enum EE { Q } en;\nextern enum EF { R } en;\n",
        "struct S { int x; };\nstruct S c;\nextern struct T *c;\n",
        "double d;\nextern _Alignas(8) int d;\n",
        "int e;\nextern _BitInt(8) e;\n",
        "int g;\nstruct { int m; } extern g;\n",
        "typedef int T;\nT x;\nvoid f(void) { extern int x; }\nvoid g(void) { extern double x; }\n",
        // A statement expression's braces and storage classes belong to it, not to the declaration (review on #1453).
        "double x;\nvoid f(void) { extern int g(typeof(({ 0; })) p), x; }\n",
        "double x;\nvoid f(void) { int a = ({ extern int y; y; }), x; double x; }\n",
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
