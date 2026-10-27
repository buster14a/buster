// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Runtime query plans are independent of source constant proofs. The real checked-source fixture below keeps
// its source proof intact while exercising the consumer's runtime-value boundary and transactional refusals.
#ifndef BUSTER_C_CHECK_PROGRAM_QUERY_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_QUERY_TEST_INCLUDED

static void check_test_program_queries(CheckTest* test, const char* clang)
{
    static const char source[] = "int tested(int n){return __builtin_constant_p(1)+n;}";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, "query-plan.c", (const uint8_t*)source, sizeof(source) - 1);
    check_test_expect(test, preprocessed, "runtime query boundary source preprocesses");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena storage = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = storage.base != NULL && output.base != NULL && bytes.base != NULL && scratch.base != NULL &&
                     check_test_types_open(&types) && parse_initialize(&parser, &parsing, pp_options.language) &&
                     parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &storage, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &storage, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &storage, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &storage, &scratch);
        const CheckProgramOptions options = {
            .initializers = &initializers,
            .asms = &asms,
            .identifiers = &identifiers,
            .scalar = path == 0,
        };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &storage, &scratch) && stream.query_count == 1;
        check_test_expect(test, ready, "source proof and query plan reach the actual program consumer");
        if (ready)
        {
            CheckBuiltinQueryEffect* effect = stream.queries;
            const CheckBuiltinQueryEffect saved = *effect;
            const CheckValue source_value = stream.values[effect->node];
            effect->runtime_value.integer = 0;
            bool truth = true;
            check_test_expect(test, check_program_runtime_truth(&program, effect->node, effect->node, &truth) && !truth &&
                                   source_value.integer == 1 && stream.values[effect->node].integer == 1,
                              "runtime query truth consumes its own known value without rewriting the source proof");
            CheckProgramCode code = { 0 };
            const bool emitted = check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                                    &output, &bytes, &scratch, NULL, NULL, false, true);
            check_test_expect(test, emitted && check_test_program_execute(&code.link, "int tested(int n){return n;}", clang, false, &scratch),
                              "native query consumer uses runtime value even when the source proof has no effect flags");
            *effect = saved;
            effect->evaluated_operands = 1;
            check_test_expect(test, check_program_runtime_effects(&program, effect->node),
                              "query operand evaluation survives branch pruning independently of source flags");
            *effect = saved;
            const uint64_t output_mark = output.length;
            const uint64_t bytes_mark = bytes.length;
            for (uint32_t mutation = 0; mutation < 5; mutation += 1)
            {
                *effect = saved;
                if (mutation == 0)
                {
                    effect->unsupported = BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_RUNTIME_PLAN;
                }
                else if (mutation == 1)
                {
                    effect->runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED;
                }
                else if (mutation == 2)
                {
                    effect->runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED;
                }
                else if (mutation == 3)
                {
                    effect->runtime_value.constant_kind = CHECK_CONSTANT_NONE;
                }
                else
                {
                    effect->evaluated_operands = 1;
                }
                CheckProgramCode rejected = { 0 };
                const bool accepted = check_program_emit(&rejected, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                                         &output, &bytes, &scratch, NULL, NULL, false, true);
                const CheckProgramStatus expected = mutation == 4 ? CHECK_PROGRAM_INVALID : CHECK_PROGRAM_UNSUPPORTED;
                check_test_expect(test, !accepted && rejected.status == expected && rejected.lowered.functions == NULL &&
                                       rejected.link.symbols.count == 0 && output.length == output_mark && bytes.length == bytes_mark &&
                                       scratch.length == 0 && program.status == CHECK_PROGRAM_OK &&
                                       memcmp(stream.values + saved.node, &source_value, sizeof(source_value)) == 0,
                                  "unresolved or malformed query plans publish no code and preserve source metadata and arena prefixes");
                if (mutation != 4)
                {
                    check_test_expect(test, !check_program_runtime_truth(&program, effect->node, effect->node, &truth),
                                      "unsupported runtime query plans cannot prune referenced branches using their source proof");
                }
            }
            *effect = saved;
        }
        check_test_expect(test, scratch.length == 0, "runtime query boundary restores scratch lifetime");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&storage);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

#endif
