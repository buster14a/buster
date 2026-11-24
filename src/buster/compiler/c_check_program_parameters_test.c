// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #792: unnamed parameters retain their signature ordinals and ABI slots. The shared native fixture runner compares
// scalar/vector reader and lowering paths, then executes the generated object beside Clang's independent build.
// Include after c_check_program_test.c's source helpers.

static void check_test_program_unnamed_parameters(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-unnamed-parameters", clang);
}
