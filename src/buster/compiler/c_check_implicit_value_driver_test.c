// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a library builtin named outside a call before any call declares it (#2348): clang declares it as
// its name is resolved, so its "call to undeclared library function" error, and its note, come before the error of the
// expression around it. Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose
// fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror
// and -w policies, with -fsyntax-only and -c.

static void check_test_driver_implicit_values(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/implicit-value", false);
}
