// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a builtin call's argument pointer warnings (#2189 item 3, c_checker_driver.c's
// check_driver_pointer_arguments): -Wpointer-sign and -Wincompatible-pointer-types-discards-qualifiers in clang's words,
// "passing 'T' to parameter of type 'U' ...", at the argument, with no parameter's note, for builtins whose arguments are a
// list (`__builtin_strcmp`) or operands (`__builtin_strcpy`). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_builtin_pointer_sign_fixtures[] =
{
    {
        .name = "builtin arguments' pointer warnings",
        .source = "int g(unsigned char *p) { return __builtin_strlen(p) + __builtin_strcmp(p, \"x\") + __builtin_strncmp(\"y\", p, 1); }\n"
                  "void h(unsigned char *d, const char *s, const unsigned char *cu) { __builtin_strcpy(d, s); __builtin_strcpy((char *)cu, cu); __builtin_strcat(d, cu); }\n"
                  "void k(const char *c) { __builtin_strcpy(c, \"x\"); __builtin_memset(c, 0, 1); }\n"
                  "#define S(x) __builtin_strlen(x)\n"
                  "unsigned long m(signed char *sp) { return S(sp); }\n",
        .warnings = 10,
        .notes = 0,
    },
};

static void check_test_driver_builtin_pointer_sign(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_builtin_pointer_sign_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_builtin_pointer_sign_fixtures));
}
