// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of C23 attribute lists before C23 (#2409, c_check_attribute.c's CHECK_WARNING_EVENT_C23_ATTRIBUTES).
// Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares
// complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_c23_attributes_fixtures[] =
{
    {
        // Each list warns at its first `[` under -pedantic: on a tag, a member, a declaration, a parameter, a declarator, an
        // enumerator, a statement, an empty one, and a pointer's.
        .name = "C23 attribute lists before C23",
        .source = "struct [[deprecated]] S { int a [[maybe_unused]]; [[maybe_unused]] int b; };\n"
                  "[[nodiscard]] [[maybe_unused]] int f(int x [[maybe_unused]]);\n"
                  "[[]] int g(int x) { switch (x) { case 1: x++; [[fallthrough]]; case 2: break; } return x; }\n"
                  "typedef int T [[deprecated]];\n"
                  "enum [[deprecated]] E { A [[deprecated]] };\n"
                  "int h(void) { [[]]; [[gnu::unused]] int y = 0; return y; }\n"
                  "void k(int *[[gnu::unused]] p);\n",
        .options = { "-pedantic" },
        .warnings = 15,
        .compare_warning_text = true,
    },
    {
        // -Wc23-extensions alone gives them, and they're ignored by default.
        .name = "C23 attribute lists under -Wc23-extensions",
        .source = "void f(int x) { [[maybe_unused]] int y = x; }\n"
                  "[[deprecated]] int g(void);\n",
        .options = { "-Wc23-extensions" },
        .warnings = 2,
        .compare_warning_text = true,
    },
    {
        .name = "C23 attribute lists by default",
        .source = "void f(int x) { [[maybe_unused]] int y = x; }\n"
                  "[[deprecated]] int g(void);\n",
        .compare_warning_text = true,
    },
};

static void check_test_driver_c23_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_c23_attributes_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_c23_attributes_fixtures));
}
