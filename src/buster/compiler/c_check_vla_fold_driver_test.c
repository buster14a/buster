// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's folding of array bounds that are no integer constant expressions (#1447): a declarator's
// whose array can't be variable (at file scope, a member's, a block's static or extern object's) is folded with
// -Wgnu-folding-constant's "variable length array folded to constant array as an extension" at its name, once for the
// declarator; a bit-field's width, an enumerator's value, a case label, a designator and a parameter's bound fold
// silently, a block's automatic array stays variable, and an alignment or a type name's bound in a file-scope initializer
// is an error. A variable-length array or variably modified type where C allows none takes clang's error at its place
// (check_input_vla_place, check_input_vla_declaration): at file scope, a block's static or extern object, a member, and a
// sizeof's type name in a file-scope initializer. Each such source has one, as the reader stops at it where clang goes
// on. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and requested
// lanes.

// A body's casts to variably modified types run as clang's build's do (#2321): each bound once, before the operand, a
// sizeof's of the cast's value read from it, nested and comma bounds too; by both paths, at Tier 0 and Tier 1.
static const char check_driver_vla_cast_program[] =
    "int printf(const char*, ...);\n"
    "static int calls;\n"
    "static int k(int v) { calls = calls * 10 + v; return v; }\n"
    "int f(int n, int *v){ return (int)sizeof(*(int (*)[n])v); }\n"
    "int g(int n, int *v){ int (*p)[n] = (int (*)[n])v; return (int)sizeof *p + p[1][0]; }\n"
    "int h(int n, int *v) { return ((int (*)[k(n)])(v + k(1)))[1][1]; }\n"
    "int s(int i) { (void)sizeof((int (*)[i++])0); return i; }\n"
    "int c2(int n, int *v) { return (int)sizeof(*(int (*)[(k(2), n)])v); }\n"
    "long d(int n, int m, int *v) { return (long)sizeof(*(int (*)[n][m])v) + ((int (*)[n][m])v)[0][1][1]; }\n"
    "int e(int n, void *v) { return (*(int (*)[n])v)[2] + (int)sizeof(*(char(*)[n + 1])(int (*)[n])v); }\n"
    "int t(int n, int *v) { return ((int (*)[k(n)][k(2)])v)[1][1][0] + calls; }\n"
    "int main(void) {\n"
    "    int a[24] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };\n"
    "    printf(\"%d %d\\n\", f(3, a), g(3, a));\n"
    "    calls = 0; printf(\"%d %d\\n\", h(4, a), calls);\n"
    "    printf(\"%d\\n\", s(5));\n"
    "    calls = 0; printf(\"%d %d\\n\", c2(5, a), calls);\n"
    "    printf(\"%ld %d\\n\", d(2, 3, a), e(4, a));\n"
    "    calls = 0; printf(\"%d\\n\", t(3, a));\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_vla_cast_runs(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const ways[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    char source[512];
    char outputs[5][512];
    snprintf(source, sizeof(source), "%s/vla-casts.c", directory);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        snprintf(outputs[k], sizeof(outputs[k]), "%s/vla-casts-%u", directory, k);
    }
    const bool written = parse_test_write(source, check_driver_vla_cast_program, strlen(check_driver_vla_cast_program));
    char* output = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-w", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &output) : 1;
    free(output);
    output = NULL;
    static char expected[4096];
    const bool referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, referenced && strlen(expected) != 0, "variably modified casts: clang's build runs");
    for (uint32_t mode = 0; referenced && mode < 4; mode += 1)
    {
        char* arguments[16] = { (char*)executable };
        uint32_t count = 1;
        if ((mode & 1) != 0)
        {
            arguments[count++] = "--scalar";
        }
        if (mode >= 2)
        {
            arguments[count++] = "--tier1";
            arguments[count++] = "--verify";
        }
        arguments[count++] = "-o";
        arguments[count++] = outputs[mode];
        arguments[count++] = source;
        arguments[count] = NULL;
        const int status = check_test_driver_spawn(arguments, &output);
        static char captured[4096];
        const bool ran = status == 0 && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "variably modified casts evaluate their bounds once, before their operands, as clang's build does (%s)", ways[mode]);
        check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
        if (status != 0 && output != NULL)
        {
            fprintf(stderr, "%s", output);
        }
        else if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "variably modified casts (%s):\n%s\nclang's build:\n%s\n", ways[mode], captured, expected);
        }
        free(output);
        output = NULL;
        unlink(outputs[mode]);
    }
    unlink(outputs[4]);
    unlink(source);
}

static void check_test_driver_vla_fold(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/vla-fold", false);
    check_test_driver_warning_catalog(test, clang, executable, directory, "checker/vla-fold");
    check_test_driver_vla_cast_runs(test, clang, executable, directory);
}
