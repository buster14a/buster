// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a C23 type attribute in a cast that a constant query reads (#2293, c_check_constant.c): a
// `_Generic`'s controlling expression, sizeof's and typeof's, whose `[[...]]` is no array bound. Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret
// diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_generic_attribute_fixtures[] =
{
    {
        .name = "C23 type attributes in constant queries' casts",
        .source = "int x = _Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 2);\n"
                  "int y = _Generic((int [[clang::annotate_type(\"a\")]])0, int: 1, default: 2);\n"
                  "int z = _Generic((int *__attribute__((address_space(1))))0, int *: 1, default: 2);\n"
                  "void f(void) { int w = _Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 2); (void)w; }\n"
                  "_Static_assert(_Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 0) == 1, \"as\");\n"
                  "_Static_assert(_Generic((int *[[clang::annotate_type(\"a\")]])0, int *: 1, default: 0), \"anp\");\n"
                  "_Static_assert(sizeof((int [[clang::annotate_type(\"b\")]] *)0) == 8, \"sz\");\n"
                  "typedef typeof((int *[[clang::address_space(2)]])0) T;\n"
                  "_Static_assert(_Generic((T)0, int __attribute__((address_space(2))) *: 1, default: 0) == 0, \"t\");\n"
                  "int v = _Generic((char (*[[clang::annotate_type(\"c\")]])[3])0, char (*)[3]: 1, default: 2);\n",
        .c23 = true,
    },
};

static void check_test_driver_generic_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_generic_attribute_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_generic_attribute_fixtures));
}
