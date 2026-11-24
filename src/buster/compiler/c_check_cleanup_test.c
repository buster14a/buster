// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's cleanup attribute (#147, c_check_cleanup_input.c), on original fixtures. Its rules against clang's: the declarations
// it applies to and those it is ignored on, its argument's lookup, its function's parameter, and the jumps into and out of
// a cleanup's scope that clang's JumpScopeChecker rejects. Then programs of each exit from a cleanup's scope, run natively
// against clang -O0's build of the same source on both paths: blocks' ends, breaks and continues of every loop and a for
// statement's own variables, gotos, returns of each class of value (kept before their cleanups) and the named return value
// (after them, as clang's NRVO has it), statement expressions, switches, and the functions' kinds the lowering takes: their
// parameters (a _Bool's and _Atomic pointers' among them) and their results (in memory, in registers, in a result object).
// Include after c_check_program_test.c and c_check_vla_test.c.
#ifndef BUSTER_C_CHECK_CLEANUP_TEST_INCLUDED
#define BUSTER_C_CHECK_CLEANUP_TEST_INCLUDED

// The attribute phase owns one primary for a type-name argument, without a second generic malformed-attribute report.
// The source parser can reject that token before this phase, so test the phase directly rather than requiring Clang's
// later attribute diagnostics after the existing parser rejection. Binding-use publication remains intact.
static void check_test_cleanup_attribute_primary(CheckTest* test)
{
    for (uint32_t duplicate = 0; duplicate < 2; duplicate += 1)
    {
        Arena arena = arena_reserve(UINT64_C(1) << 15);
        uint8_t kinds[4] = { PARSE_KIND_IDENTIFIER, PARSE_KIND_IDENTIFIER, PARSE_KIND_IDENTIFIER, PARSE_KIND_IDENTIFIER };
        uint32_t names[4] = { 0, 1, 0, 2 };
        Parser parser = { .tokens = { .kinds = kinds, .names = names, .count = 4 } };
        CheckInputReport reports[4];
        CheckInput input = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
        CheckInputReader reader = { .parser = &parser, .output = &input, .arena = &arena };
        const bool ready = arena.base != NULL && c_bindings_initialize(&reader.bindings, &arena, 3, 8) &&
                           c_bindings_declare(&reader.bindings, C_BINDING_ORDINARY, 1, C_BINDING_TYPEDEF, 0, 0, 0) != 0 &&
                           c_bindings_declare(&reader.bindings, C_BINDING_ORDINARY, 2, C_BINDING_FUNCTION, 2, 0, 0) != 0;
        check_test_expect(test, ready, "cleanup attribute primary fixture has binding/report storage");
        if (ready)
        {
            CheckInputAttribute records[2] =
            {
                { .name_token = 0, .argument_first = 1, .argument_end = 2, .end = 2, .kind = CHECK_ATTRIBUTE_CLEANUP, .schema = CHECK_ATTRIBUTE_IDENTIFIER },
                { .name_token = 2, .argument_first = 3, .argument_end = 4, .end = 4, .kind = CHECK_ATTRIBUTE_CLEANUP, .schema = CHECK_ATTRIBUTE_IDENTIFIER },
            };
            CheckInputAttributes attributes = { .records = records, .count = 1 + duplicate };
            bool valid = true;
            for (uint32_t item = 0; valid && item < attributes.count; item += 1)
            {
                valid = check_input_attribute_validate(&reader, &attributes, records + item, 1, 1);
            }
            check_test_expect(test, !valid && !input.resource && input.report_count == 1 && reports[0].token == 1 &&
                              reports[0].message == CHECK_INPUT_CLEANUP_FUNCTION && attributes.error == CHECK_ATTRIBUTE_ERROR_IDENTIFIER &&
                              attributes.error_first == 1 && attributes.error_end == 2,
                              "type-name cleanup argument has exactly its own located attribute primary");
            check_test_expect(test, reader.bindings.valid && reader.bindings.count == 3 && reader.bindings.operation[2] == C_BINDING_USE &&
                              reader.bindings.declaration[2] == 1 && reader.bindings.point[2] == 1 && records[0].cleanup_function == 1 &&
                              records[1].cleanup_function == 0,
                              "the rejected cleanup argument retains its binding use before a later duplicate");
            check_input_attribute_error(&reader, &attributes, 0, 2, CHECK_ATTRIBUTE_ERROR_SYNTAX);
            check_test_expect(test, input.report_count == 1 && attributes.error_first == 1 && attributes.error_end == 2,
                              "generic attribute fallback preserves the existing typed primary");
        }
        arena_release(&arena);
    }
}

static __attribute__((noinline)) void check_test_cleanup_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/cleanup, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/cleanup", "cleanup Clang oracle could not run");
}

// Each program's cleanups log the values they see into `trace`, which tested folds into its result.

static __attribute__((noinline)) void check_test_cleanup_programs(CheckTest* test, const char* clang)
{
    // Both declarators inherit the argument parsed before the first declarator shadows the function's name.
    check_test_program_suite(test, "checker/programs/cleanup-programs", clang);
    // An _Atomic _Bool's, which clang 23's instruction selection can't select ("Cannot select: bitcast FrameIndex"), is
    // refused by name.
    check_test_program_boundary_reason(test, "static void noted(_Atomic _Bool b){(void)b;}int tested(int n){int a __attribute__((cleanup(noted)))=n;return a;}",
                                       "cleanup-atomic-bool-parameter", true, false, 1, 1, "cleanup functions of an _Atomic _Bool parameter");
    check_test_program_build_release();
}

static __attribute__((noinline)) void check_test_cleanups(CheckTest* test, const char* clang)
{
    check_test_cleanup_attribute_primary(test);
    check_test_cleanup_sources(test, clang);
    check_test_cleanup_programs(test, clang);
}

#endif
