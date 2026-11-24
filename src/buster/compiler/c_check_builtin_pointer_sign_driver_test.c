// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources for a builtin call's argument pointer warnings (#2189 item 3, c_checker_driver.c's
// check_driver_pointer_arguments): -Wpointer-sign and -Wincompatible-pointer-types-discards-qualifiers in clang's words,
// "passing 'T' to parameter of type 'U' ...", at the argument, with no parameter's note, for builtins whose arguments are a
// list (`__builtin_strcmp`) or operands (`__builtin_strcpy`). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c. Also #2189's other items: CFString's
// argument of no ordinary literal (check_semantic_expression), dump_struct's rest argument that doesn't convert to its
// printer's parameter with clang's two notes (check_driver_dump_struct), and verbose_trap through a const pointer whose
// own initializer lost the designator (check_input_verbose_trap_object). And #2460: a builtin's argument that doesn't
// convert, in the conversion's words (check_driver_builtin_conversion).

static void check_test_driver_builtin_pointer_sign(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/builtin-pointer-sign", false);
}
