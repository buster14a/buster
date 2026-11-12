// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of clang's conversions that default to errors, demoted or ignored (#1963, c_checker_driver.c's
// check_driver_default_error_warnings). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and
// lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

#define BUSTER_CHECK_DEFAULT_ERROR_BODIES \
    "void take(char *p);\n" \
    "int *f(long x, int *q)\n" \
    "{\n" \
    "    int *p = x; char *c = p; void (*fp)(int) = (void (*)(void))0;\n" \
    "    (void)c; (void)fp; p = 3; take(q); return x;\n" \
    "}\n" \
    "void g(void) { return 1; }\n" \
    "int h(void) { return; }\n"

static const CheckDriverDefaultWarningFixture check_driver_default_error_fixtures[] =
{
    {
        // Each group demoted: an initializer's, an assignment's, an argument's (with its parameter's note) and a return's
        // conversion, a function pointer's, and both -Wreturn-mismatch cases.
        .name = "default-error conversions demoted",
        .source = BUSTER_CHECK_DEFAULT_ERROR_BODIES,
        .options = { "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types", "-Wno-error=return-mismatch" },
        .warnings = 8,
        .notes = 1,
    },
    {
        // One group demoted (with its function pointers' group under it), the others errors still.
        .name = "default-error conversions, one group demoted",
        .source = BUSTER_CHECK_DEFAULT_ERROR_BODIES,
        .options = { "-Wno-error=incompatible-pointer-types" },
        .warnings = 3,
        .errors = 5,
        .notes = 1,
    },
    {
        // Ignored and demoted through the groups that hold them: -Wconversion, -Wnon-gcc, -Wreturn-type and -Wall.
        .name = "default-error conversions through their parent groups",
        .source = BUSTER_CHECK_DEFAULT_ERROR_BODIES,
        .options = { "-Wno-error=non-gcc", "-Wno-incompatible-function-pointer-types", "-Wno-all" },
        .warnings = 3,
        .errors = 2,
        .notes = 1,
    },
    {
        .name = "default-error conversions ignored",
        .source = BUSTER_CHECK_DEFAULT_ERROR_BODIES,
        .options = { "-Wno-int-conversion", "-Wno-error=return-type" },
        .warnings = 2,
        .errors = 3,
        .notes = 1,
    },
    {
        // File-scope and local initializers, braced, nested and a compound literal's, of each group.
        .name = "default-error conversions in initializers",
        .source = "int g0;\n"
                  "int *gp = 5;\n"
                  "char *gc = &g0;\n"
                  "struct S { int *m; long n; } s = { 7, &g0 };\n"
                  "long arr[2] = { &g0, 0 };\n"
                  "void f(int *q)\n"
                  "{\n"
                  "    struct S t = { q, q };\n"
                  "    char *u[] = { q, 0 };\n"
                  "    int *w = (int *[]){ 3 }[0];\n"
                  "    (void)t; (void)u; (void)w;\n"
                  "}\n",
        .options = { "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types" },
        .warnings = 8,
    },
    {
        // A call's demoted arguments among its other conversion warnings, in its arguments' order (#2481).
        .name = "default-error conversions among a call's warnings",
        .source = "void t(char *a, int *b, char *c);\n"
                  "void f(const char *x, char *y, long z) { t(x, z, x); t(y, (char *)0, x); }\n",
        .options = { "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types" },
        .warnings = 5,
        .notes = 5,
    },
    {
        // Pragmas: a group's warning, ignored and error, and push and pop.
        .name = "default-error conversions' pragmas",
        .source = "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic warning \"-Wint-conversion\"\n"
                  "int *a(long x) { return x; }\n"
                  "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
                  "char *b(int *q) { return q; }\n"
                  "void (*c(void (*p)(void)))(int) { return p; }\n"
                  "#pragma clang diagnostic pop\n"
                  "char *d(int *q) { return q; }\n"
                  "#pragma clang diagnostic error \"-Wint-conversion\"\n"
                  "long e(int *q) { return q; }\n"
                  "#pragma clang diagnostic warning \"-Wreturn-mismatch\"\n"
                  "int r(void) { return; }\n"
                  "void v(void) { return 1; }\n",
        .options = { "-Wno-error=incompatible-pointer-types" },
        .warnings = 4,
        .errors = 1,
    },
};

#undef BUSTER_CHECK_DEFAULT_ERROR_BODIES

static void check_test_driver_default_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_default_error_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_default_error_fixtures));
}
