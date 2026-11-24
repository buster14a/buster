// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wunevaluated-expression (#1447, CheckUnaryExprOrTypeTraitOperand and
// CreateGenericSelectionExpr, c_check_warning_graph.c's check_warning_graph_operand): an unevaluated sizeof, alignof or
// _Generic operand with a side effect, on by default under -Wunused-value. The shared warning harness compares the
// pinned Clang's complete diagnostics through both kernels and requested lanes. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static void check_test_driver_unevaluated(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/unevaluated", false);
}
