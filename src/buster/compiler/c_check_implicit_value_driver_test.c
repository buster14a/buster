// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a library builtin named outside a call before any call declares it (#2348): clang declares it as
// its name is resolved, so its "call to undeclared library function" error, and its note, come before the error of the
// expression around it. Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose
// fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror
// and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_implicit_value_fixtures[] =
{
    {
        .name = "library builtins named outside calls",
        .source = "void f(void) { int *q = malloc; }\n"
                  "void g(void) { int x = strlen; }\n"
                  "void h(void) { int *r = &malloc; void *s = calloc; (void)s; }\n",
        .errors = 6,
        .notes = 3,
    },
    {
        .name = "library builtins named outside calls before C99",
        .source = "int g(void) { return abs; }\n"
                  "void h(void) { void (*fp)(void) = free; (void)fp; }\n",
        .options = { "-std=gnu89" },
        .warnings = 2,
        .errors = 2,
        .notes = 2,
    },
    {
        // Before C99 the implicit declarations are warnings, so -Werror's errors carry clang's "-Werror," prefix.
        .name = "implicit declarations before C99, with -Wall",
        .source = "int g(void) { return abs(1) + foo(2); }\n",
        .options = { "-std=gnu89", "-Wall" },
        .warnings = 2,
        .notes = 1,
    },
};

static void check_test_driver_implicit_values(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_implicit_value_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_implicit_value_fixtures));
}
