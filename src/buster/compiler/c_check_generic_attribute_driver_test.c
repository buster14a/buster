// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a C23 type attribute in a cast that a constant query reads (#2293, c_check_constant.c): a
// `_Generic`'s controlling expression, sizeof's and typeof's, whose `[[...]]` is no array bound. Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret
// diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static void check_test_driver_generic_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/generic-attribute", false);
}
