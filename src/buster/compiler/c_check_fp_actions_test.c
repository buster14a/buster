// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Distinguish source constant proofs from executable floating-environment dependence. Runtime oracles live in
// the program tests; these checks cover each producer boundary and both bounded graph schedules.
#ifndef BUSTER_C_CHECK_FP_ACTIONS_TEST_INCLUDED
#define BUSTER_C_CHECK_FP_ACTIONS_TEST_INCLUDED

// The call guard must not clear an inherited witness, mark integer-only actions, or disturb an error.
// Mix those cases with newly sensitive arithmetic/conversions across complete and partial action batches.
static void check_test_fp_action_guards(CheckTest* test, TypeTable* table)
{
    CheckValue inherited = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    inherited.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
    const CheckAction fixtures[] =
    {
        { .op = CHECK_OP_DIVIDE, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 1), check_test_float(BUSTER_TYPE_DOUBLE, 3) } },
        { .op = CHECK_OP_CAST, .operands = { check_test_integer(BUSTER_TYPE_INT, 16777217) }, .type = BUSTER_TYPE_FLOAT },
        { .op = CHECK_OP_SELECT, .operands = { inherited } },
        { .op = CHECK_OP_ADD, .operands = { check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_INT, 2) } },
        { .op = CHECK_OP_CAST, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 1) }, .type = 0 },
        { .op = CHECK_OP_LOGICAL_AND, .operands = { check_test_integer(BUSTER_TYPE_INT, 0), inherited } },
    };
    for (uint32_t rotation = 0; rotation < BUSTER_ARRAY_LENGTH(fixtures); rotation += 1)
    {
        for (uint32_t phase = 0; phase < 2; phase += 1)
        {
            for (uint32_t count = 0; count <= 17; count += 1)
            {
                CheckAction actions[17];
                CheckActionResult scalar[17];
                CheckActionResult vector[18];
                CheckActionResult untouched;
                memset(vector, 0xa5, sizeof(vector));
                memset(&untouched, 0xa5, sizeof(untouched));
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t kind = (rotation + i) % BUSTER_ARRAY_LENGTH(fixtures);
                    const bool enabled = ((i + phase) & 1) != 0;
                    actions[i] = fixtures[kind];
                    actions[i].context.fp_environment = enabled ? BUSTER_CHECK_FP_ENV_ACCESS : 0;
                    check_action_scalar(table, actions + i, scalar + i);
                    const bool marked = kind == 2 || (kind < 2 && enabled);
                    check_test_expect(test, ((scalar[i].value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0) == marked &&
                                           ((scalar[i].diagnostic != CHECK_DIAG_NONE) == (kind == 4)),
                                      "ACCESS call guard preserves new/inherited witnesses, short circuiting and invalid actions");
                }
                check_actions(table, actions, vector, count);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_test_expect(test, check_test_action_equal(scalar + i, vector + i),
                                      "mixed ACCESS-on/off action batches match scalar values, conversions and diagnostics");
                }
                check_test_expect(test, memcmp(vector + count, &untouched, sizeof(untouched)) == 0,
                                  "empty and partial ACCESS action batches leave the following result untouched");
            }
        }
    }
}

static void check_test_fp_action_values(CheckTest* test, TypeTable* table)
{
    CheckAction divide = { .op = CHECK_OP_DIVIDE, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 1),
                                                              check_test_float(BUSTER_TYPE_DOUBLE, 3) } };
    const CheckActionResult ordinary = check_test_action(test, table, divide);
    divide.context.fp_environment = BUSTER_CHECK_FP_ENV_ACCESS;
    const CheckActionResult strict = check_test_action(test, table, divide);
    check_test_expect(test, strict.diagnostic == CHECK_DIAG_NONE && strict.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                           strict.value.floating == ordinary.value.floating && (strict.value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0 &&
                           (strict.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && (ordinary.value.flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0,
                      "FENV_ACCESS preserves default source folding separately from mandatory runtime floating work");
    const CheckAction operations[] = {
        { .op = CHECK_OP_CAST, .operands = { check_test_integer(BUSTER_TYPE_INT, 16777217) }, .type = BUSTER_TYPE_FLOAT },
        { .op = CHECK_OP_CAST, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 0.1L) }, .type = BUSTER_TYPE_FLOAT },
        { .op = CHECK_OP_CAST, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 3.7L) }, .type = BUSTER_TYPE_INT },
        { .op = CHECK_OP_INITIALIZE, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 0.1L) }, .type = BUSTER_TYPE_FLOAT },
        { .op = CHECK_OP_ADD, .operands = { check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_INT, 2) } },
        { .op = CHECK_OP_CAST, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 0.1L) }, .type = BUSTER_TYPE_DOUBLE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        CheckAction action = operations[i];
        action.context.fp_environment = BUSTER_CHECK_FP_ENV_ACCESS;
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind != CHECK_CONSTANT_NONE &&
                               ((result.value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0) == (i < 4),
                          "explicit and implicit numeric conversions retain runtime FP effects without tainting integer or identity operations");
    }
    const CheckAction selected[] = {
        { .op = CHECK_OP_LOGICAL_AND, .operands = { check_test_integer(BUSTER_TYPE_INT, 0), strict.value } },
        { .op = CHECK_OP_CONDITIONAL, .operands = { check_test_integer(BUSTER_TYPE_INT, 1), check_test_float(BUSTER_TYPE_DOUBLE, 2), strict.value } },
        { .op = CHECK_OP_SIZEOF, .operands = { strict.value } },
        { .op = CHECK_OP_SELECT, .operands = { strict.value } },
        { .op = CHECK_OP_IMAGINARY, .operands = { strict.value } },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(selected); i += 1)
    {
        CheckAction action = selected[i];
        action.context.fp_environment = BUSTER_CHECK_FP_ENV_ACCESS;
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE &&
                               ((result.value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0) == (i >= 3),
                          "only evaluated floating-environment dependencies survive selection, sizeof and scalar imaginary extraction");
    }
}

static void check_test_fp_graph(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(262144);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    uint8_t environments[32] = { 0 };
    const bool ready = check_graph_initialize(&graph, &storage, 32, 32);
    check_test_expect(test, ready, "floating-environment graph allocation");
    if (ready)
    {
        graph.fp_environments = environments;
        const uint32_t one = check_test_leaf(&graph, 1, check_test_float(BUSTER_TYPE_DOUBLE, 1));
        const uint32_t three = check_test_leaf(&graph, 2, check_test_float(BUSTER_TYPE_DOUBLE, 3));
        const uint32_t divided = check_graph_add(&graph, CHECK_OP_DIVIDE, 3, one, three, BUSTER_CHECK_NONE);
        environments[divided] = BUSTER_CHECK_FP_ENV_ACCESS;
        const uint32_t constant = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &divided, 1);
        const uint32_t integer = check_test_leaf(&graph, 5, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t ancestor = check_graph_add(&graph, CHECK_OP_ADD, 6, constant, integer, BUSTER_CHECK_NONE);
        uint32_t classify_arguments[6];
        for (uint32_t i = 0; i < 5; i += 1)
        {
            classify_arguments[i] = check_test_leaf(&graph, 7 + i, check_test_integer(BUSTER_TYPE_INT, 10 + i));
        }
        classify_arguments[5] = divided;
        const uint32_t classify = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_FPCLASSIFY, classify_arguments, 6);
        const uint32_t sign = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_ISINF_SIGN, &one, 1);
        environments[sign] = BUSTER_CHECK_FP_ENV_ACCESS;
        const uint32_t math_arguments[] = { divided, three };
        const uint32_t copy = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_COPYSIGNF, math_arguments, 2);
        environments[copy] = BUSTER_CHECK_FP_ENV_ACCESS;
        const uint32_t square = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_SQRT, &three, 1);
        CheckValue updated = check_test_integer(BUSTER_TYPE_INT, 0);
        updated.flags |= BUSTER_CHECK_SIDE_EFFECT;
        const uint32_t update = check_test_leaf(&graph, 18, updated);
        const uint32_t comma = check_graph_add(&graph, CHECK_OP_COMMA, 19, update, divided, BUSTER_CHECK_NONE);
        const uint32_t comma_query = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &comma, 1);
        const CheckValue called = { .type = BUSTER_TYPE_DOUBLE, .category = CHECK_RVALUE,
                                    .flags = BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT };
        const uint32_t call = check_test_leaf(&graph, 21, called);
        const uint32_t call_query = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &call, 1);
        CheckStream scalar;
        const CheckOptions options = { .scalar = true };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &scalar);
        check_test_expect(test, checked && scalar.report_count == 0 && scalar.query_count == 3 && scalar.floating_count == 4,
                          "FENV and math graph actions retain valid source constraints");
        if (checked)
        {
            check_test_expect(test, check_stream_value(&scalar, constant).integer == 1 && scalar.queries[0].runtime_value.integer == 0 &&
                                   scalar.queries[0].evaluated_operands == 1 && check_stream_value(&scalar, ancestor).integer == 2 &&
                                   (check_stream_flags(&scalar, ancestor) & BUSTER_CHECK_FP_ENVIRONMENT) != 0,
                              "source constant_p proof and executable false result retain distinct values and a transitive runtime witness");
            check_test_expect(test, check_stream_value(&scalar, comma_query).integer == 0 && check_stream_value(&scalar, call_query).integer == 0 &&
                                   scalar.queries[1].evaluated_operands == 0 && scalar.queries[2].evaluated_operands == 0 &&
                                   scalar.queries[1].runtime_value.integer == 0 && scalar.queries[2].runtime_value.integer == 0 &&
                                   (check_stream_flags(&scalar, comma_query) & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                                   (check_stream_flags(&scalar, call_query) & BUSTER_CHECK_FP_ENVIRONMENT) == 0,
                              "constant_p leaves marked comma and call operands unevaluated when their source proof is false");
            check_test_expect(test, check_stream_value(&scalar, classify).integer == 12 && scalar.floating[0].selected_selector == BUSTER_CHECK_BUILTIN_FLOAT_NO_SELECTOR &&
                                   scalar.floating[0].evaluated_operands == 63 && scalar.floating[0].definite_operands == 48 &&
                                   scalar.floating[0].evaluation == CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME && !scalar.floating[0].folded &&
                                   scalar.floating[1].node == sign && scalar.floating[1].may_raise_fp_exceptions,
                              "environment-dependent classification retains all runtime selector paths and strict comparison exceptions");
            check_test_expect(test, check_stream_type(&scalar, copy) == BUSTER_TYPE_FLOAT && check_stream_constant_kind(&scalar, copy) == CHECK_CONSTANT_FLOATING &&
                                   scalar.floating[2].runtime_builtin == CBUILTIN_COPYSIGNF && !scalar.floating[2].folded &&
                                   scalar.floating[2].converted_arguments[0].type == BUSTER_TYPE_FLOAT &&
                                   scalar.floating[2].compiler_barrier && scalar.floating[2].may_raise_inexact &&
                                   check_stream_type(&scalar, square) == BUSTER_TYPE_DOUBLE && check_stream_constant_kind(&scalar, square) == CHECK_CONSTANT_NONE &&
                                   scalar.floating[3].may_set_errno && scalar.floating[3].errno_pointer_type == type_pointer(table, BUSTER_TYPE_INT),
                              "math descriptors retain source conversions, runtime environment requirements and a real errno pointer type");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                CheckStream vector;
                const CheckOptions partition = { .batch_size = batch };
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &partition, &vector) &&
                                       check_test_stream_equal(&scalar, &vector, graph.argument_count),
                                  "all graph partitions preserve FENV proofs, runtime masks and complete math descriptors");
                arena_pop_to(&output, mark);
            }
            environments[divided] = 2;
            CheckStream invalid;
            check_test_expect(test, !check_graph_run(table, &graph, &output, &scratch, &options, &invalid) &&
                                   invalid.status == CHECK_STATUS_INVALID_GRAPH && output.length == mark && scratch.length == 0,
                              "unknown floating-environment bits reject atomically before graph publication");
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

static __attribute__((noinline)) void check_test_fp_actions(CheckTest* test, TypeTable* table)
{
    check_test_fp_action_guards(test, table);
    check_test_fp_action_values(test, table);
    check_test_fp_graph(test, table);
}

#endif
