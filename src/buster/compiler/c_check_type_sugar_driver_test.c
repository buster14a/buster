// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Type sugar in diagnostics (#1447): original fixtures whose whole diagnostic output is compared with clang's on both
// paths, at each warning policy, with -fsyntax-only and -c, on one and three lanes. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

// A variable array's size expression as written (#1447, check_driver_vla_bounds), through pointers, arrays, typedefs
// and conversions, and an anonymous tag's location.

// Calling conventions in type spellings (#1447): clang's canonical spelling in an a.k.a. (`intel_ocl_bicc`,
// swiftasynccall without its space), a function designator's preserve_* before it, and a convention one macro writes
// named by the macro before the canonical spelling (check_driver_convention_macros).

// clang's const assignment diagnostics (#1447, check_semantic_const_assignment and check_semantic_const_fields): the
// first const level of a member, subscript and call chain with a note per const level, and a record with const members
// with a note per member, breadth-first.

// size_t's __size_t sugar (#1447 item 10, check_driver_size_sugar and check_driver_library_designator): an implicitly
// declared library builtin's designator and address by its prototype, its call's value, and arithmetic as clang's usual
// arithmetic conversions keep the sugar of the operand the result takes its type from.

// A failed static assertion's details (#2171, check_semantic_assert): clang's "expression evaluates to" note, the failing
// conjunct of an `&&`, and its requirement's number literals as clang's StmtPrinter prints them.

// A parameter's typedef in a printed type (#1447 item 5, check_driver_placed_type): the declaration's own specifiers' and
// each nested parameter's typedef by place, with clang's a.k.a. of the type without them, for an object, a function, its
// address, an array's element, a typedef's use, a call's parameter, an initialized, assigned or passed target.

// A counted_by family member's type as clang's CountAttributedType prints it (#1842's 3c, check_driver_counted_type): each
// keyword's, a pointer's attribute before the placeholder and a flexible array's after it, through `&` and an array's
// decay, as a failed conversion's source or an assigned member target, with the a.k.a.; a sibling declarator without
// one keeps none.

// A use of a counted_by or counted_by_or_null pointer member whose pointee type is incomplete there (#1842's 3d,
// check_graph_counted_uses): clang's "cannot use" at a read (a condition's, a `.`'s, a return's) and "cannot assign to" at
// an assignment, each with its two notes; none for `&`'s operand, a sized_by member, or a pointee completed before the use.

// Vector member failures retain the typed action's decision and specialize only its diagnostic (#2096).

static void check_test_driver_type_sugar(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/type-sugar", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/const-assignment", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/convention-sugar", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/vla-bound", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/size-sugar", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/static-assert", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/counted-incomplete", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/counted-sugar", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/parameter-typedef", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/vector-member", false);
}
