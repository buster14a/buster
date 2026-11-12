// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of a static assertion's condition converted to a boolean (#2409, c_check_common_warnings.c's
// check_common_assertion_reach). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_assert_bool_fixtures[] =
{
    {
        // A `<<` of literals and a conditional of integer literals as a static assertion's condition, at file scope, among
        // a record's members and in a block, where the shift's count gives no warning of its own; a runtime one's does.
        .name = "static assertions' conditions converted to booleans",
        .source = "_Static_assert(1 << 40, \"big\");\n"
                  "struct Y { int a; _Static_assert(1 << 40, \"big\"); };\n"
                  "void f(void) { _Static_assert(1 << 40, \"big\"); if (1 << 40) {} }\n"
                  "_Static_assert(1 << 4, \"a\");\n"
                  "_Static_assert(2 ? 3 : 4, \"c\");\n"
                  "_Static_assert(1 ? 1 : 0, \"d\");\n"
                  "_Static_assert((1 << 4) != 0, \"e\");\n",
        .warnings = 7,
        .compare_warning_text = true,
    },
};

static void check_test_driver_assert_bool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_assert_bool_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_assert_bool_fixtures));
}
