// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Distinguish source constant proofs from executable floating-environment dependence. Runtime oracles live in
// the program tests; these checks cover each producer boundary and both bounded graph schedules.
#ifndef BUSTER_C_CHECK_FP_ACTIONS_TEST_INCLUDED
#define BUSTER_C_CHECK_FP_ACTIONS_TEST_INCLUDED

#include "c_check_pragma_extensions_test.c"

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

// These are conversion boundaries, not just final result types: compound assignment skips the left operand's
// evaluation promotion, increment skips both, and variadic float arguments still pass as double under extended.
static void check_test_fp_evaluation_actions(CheckTest* test, TypeTable* table)
{
    const uint8_t environments[] = { 0, BUSTER_CHECK_FP_EVAL_DOUBLE, BUSTER_CHECK_FP_EVAL_EXTENDED };
    const TypeId methods[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    const TypeId parameter = BUSTER_TYPE_FLOAT;
    const TypeId function = type_function(table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC);
    const TypeId vector = type_vector(table, BUSTER_TYPE_FLOAT, 4, true);
    for (uint32_t method = 0; method < BUSTER_ARRAY_LENGTH(environments); method += 1)
    {
        const CheckContext context = { .fp_environment = environments[method] };
        const CheckValue operands[] = { check_test_float(BUSTER_TYPE_FLOAT, 1), check_test_float(BUSTER_TYPE_FLOAT, 2) };
        const CheckOp ordinary[] = { CHECK_OP_ADD, CHECK_OP_POSITIVE, CHECK_OP_NEGATIVE };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ordinary); i += 1)
        {
            const CheckActionResult result = check_test_action(test, table, (CheckAction){ .op = ordinary[i], .operands = { operands[0], operands[1] }, .context = context });
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == methods[method] && result.conversions[0].type == methods[method],
                              "usual unary and arithmetic conversions carry the active evaluation type");
        }
        const CheckOp logical[] = { CHECK_OP_NOT, CHECK_OP_LOGICAL_AND, CHECK_OP_LOGICAL_OR };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(logical); i += 1)
        {
            const CheckActionResult result = check_test_action(test, table, (CheckAction){ .op = logical[i], .operands = { operands[0], operands[1] }, .context = context });
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_INT &&
                                   result.conversions[0].type == (i == 0 ? BUSTER_TYPE_FLOAT : methods[method]),
                              "binary logical operands use evaluation promotion while unary not uses only lvalue conversion");
        }
        const CheckValue object = { .type = BUSTER_TYPE_FLOAT, .category = CHECK_LVALUE };
        const CheckActionResult integer_update = check_test_action(test, table, (CheckAction){ .op = CHECK_OP_ADD_ASSIGN,
            .operands = { object, check_test_integer(BUSTER_TYPE_INT, 16777217) }, .context = context });
        const CheckActionResult float_update = check_test_action(test, table, (CheckAction){ .op = CHECK_OP_ADD_ASSIGN,
            .operands = { object, operands[1] }, .context = context });
        const CheckActionResult increment = check_test_action(test, table, (CheckAction){ .op = CHECK_OP_PRE_INCREMENT,
            .operands = { object }, .context = context });
        check_test_expect(test, integer_update.diagnostic == CHECK_DIAG_NONE && integer_update.conversions[0].type == BUSTER_TYPE_FLOAT &&
                               integer_update.conversions[1].type == BUSTER_TYPE_FLOAT && integer_update.value.type == BUSTER_TYPE_FLOAT &&
                               float_update.diagnostic == CHECK_DIAG_NONE && float_update.conversions[0].type == methods[method] &&
                               float_update.conversions[1].type == methods[method] && float_update.store_conversion.type == BUSTER_TYPE_FLOAT &&
                               increment.diagnostic == CHECK_DIAG_NONE && increment.conversions[0].type == BUSTER_TYPE_FLOAT,
                          "compound assignment promotes only its source operand and increment retains the destination format");
        const CheckValue arms = { .type = vector };
        const CheckActionResult conditional = check_test_action(test, table, (CheckAction){ .op = CHECK_OP_CONDITIONAL,
            .operands = { operands[0], arms, arms }, .context = context });
        check_test_expect(test, conditional.diagnostic == CHECK_DIAG_NONE && conditional.value.type == vector && conditional.conversions[0].type == methods[method],
                          "a scalar condition promotes even when vector arms retain their element formats");
        CheckBuiltinFloatResult comparison, classifier;
        const CheckBuiltinFloatAction compared = { .id = CBUILTIN_ISGREATER, .arguments = operands, .argument_count = 2, .context = context };
        const CheckBuiltinFloatAction classified = { .id = CBUILTIN_ISNAN, .arguments = operands, .argument_count = 1, .context = context };
        check_builtin_float_one(table, &compared, &comparison);
        check_builtin_float_one(table, &classified, &classifier);
        check_test_expect(test, comparison.action.diagnostic == CHECK_DIAG_NONE && comparison.floating_type == methods[method] &&
                               classifier.action.diagnostic == CHECK_DIAG_NONE && classifier.floating_type == BUSTER_TYPE_FLOAT,
                          "ordered floating builtins promote arithmetic operands while classifiers retain their source format");
        CheckValue arguments[] = { operands[0], operands[1], check_test_float(BUSTER_TYPE_DOUBLE, 3) };
        CheckConversion conversions[BUSTER_ARRAY_LENGTH(arguments)] = { 0 };
        const CheckAction call = { .op = CHECK_OP_CALL, .operands = { { .type = function, .category = CHECK_FUNCTION } }, .arguments = arguments,
                                  .argument_count = BUSTER_ARRAY_LENGTH(arguments), .argument_conversions = conversions, .context = context };
        const CheckActionResult called = check_test_action(test, table, call);
        check_test_expect(test, called.diagnostic == CHECK_DIAG_NONE && conversions[0].type == BUSTER_TYPE_FLOAT && conversions[1].type == BUSTER_TYPE_DOUBLE &&
                               conversions[2].type == (method == 2 ? BUSTER_TYPE_LONG_DOUBLE : BUSTER_TYPE_DOUBLE),
                          "fixed prototype arguments keep declared types and variadic float keeps its double ABI under extended evaluation");
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
            for (uint32_t batch = 0; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                CheckStream vector;
                const CheckOptions partition = { .batch_size = batch, .regions = batch != 0 };
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &partition, &vector) &&
                                       check_test_stream_equal(&scalar, &vector, graph.argument_count),
                                  "all graph partitions preserve FENV proofs, runtime masks and complete math descriptors");
                arena_pop_to(&output, mark);
            }
            environments[divided] = BUSTER_CHECK_FP_CONTRACT_MASK;
            CheckStream invalid;
            check_test_expect(test, !check_graph_run(table, &graph, &output, &scratch, &options, &invalid) &&
                                   invalid.status == CHECK_STATUS_INVALID_GRAPH && output.length == mark && scratch.length == 0,
                              "contradictory floating contraction bits reject atomically before graph publication");
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

// Exercise the x87 return ABI too: inlining the decoder could hide a signaling NaN changed by a host load/store.
static __attribute__((noinline)) long double check_test_half_decode(CheckHalf format, uint16_t bits)
{
    return check_action_half_value(format, bits);
}

// Representation, not arithmetic: decoding and re-encoding every 16-bit value must preserve all bits. The raw
// x87 field fixtures are independent of the encoder and distinguish payload preservation from canonicalization.
static void check_test_half_encodings(CheckTest* test)
{
    const CheckHalf formats[] = { CHECK_HALF_IEEE, CHECK_HALF_BRAIN };
    for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
    {
        bool exact = true;
        for (uint32_t bits = 0; bits <= UINT16_MAX; bits += 1)
        {
            const long double value = check_test_half_decode(formats[format], (uint16_t)bits);
            exact &= check_action_half_bits(formats[format], value) == bits;
        }
        check_test_expect(test, exact, "every half encoding survives decode, the host return ABI and re-encoding");
    }
    static const struct
    {
        CheckHalf format;
        uint16_t bits;
        uint64_t significand;
        uint16_t exponent;
    } nans[] = {
        { CHECK_HALF_IEEE, 0x7d00, UINT64_C(0xa000000000000000), 0x7fff },
        { CHECK_HALF_IEEE, 0xfd05, UINT64_C(0xa0a0000000000000), 0xffff },
        { CHECK_HALF_IEEE, 0x7e35, UINT64_C(0xc6a0000000000000), 0x7fff },
        { CHECK_HALF_IEEE, 0xffff, UINT64_C(0xffe0000000000000), 0xffff },
        { CHECK_HALF_BRAIN, 0x7f81, UINT64_C(0x8100000000000000), 0x7fff },
        { CHECK_HALF_BRAIN, 0xffa5, UINT64_C(0xa500000000000000), 0xffff },
        { CHECK_HALF_BRAIN, 0x7fc5, UINT64_C(0xc500000000000000), 0x7fff },
        { CHECK_HALF_BRAIN, 0xffff, UINT64_C(0xff00000000000000), 0xffff },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(nans); i += 1)
    {
        const long double value = check_test_half_decode(nans[i].format, nans[i].bits);
        uint64_t significand = 0;
        uint16_t exponent = 0;
        memcpy(&significand, &value, sizeof(significand));
        memcpy(&exponent, (const uint8_t*)&value + 8, sizeof(exponent));
        check_test_expect(test, significand == nans[i].significand && exponent == nans[i].exponent,
                          "half NaN decoding preserves raw sign, signaling and payload fields");
    }
}

// Source constant evaluation and executable half arithmetic have different rounding boundaries (#1497).
// Keep an independent expected value for each operation and ensure runtime action batches never acquire it.
static void check_test_half_constant_actions(CheckTest* test, TypeTable* table)
{
    const TypeId formats[] = { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_BF16 };
    const CheckOp operations[] = { CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE };
    const long double expected[] = { 3, 1, 2, 2 };
    for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
    {
        for (uint32_t op = 0; op < BUSTER_ARRAY_LENGTH(operations); op += 1)
        {
            CheckAction action = {
                .op = operations[op],
                .operands = { check_test_float(formats[format], 2), check_test_float(formats[format], 1) },
                .context = { .constant_evaluated = true },
            };
            const CheckActionResult constant = check_test_action(test, table, action);
            check_test_expect(test, constant.diagnostic == CHECK_DIAG_NONE && constant.value.fault == CHECK_FAULT_NONE &&
                                   constant.value.constant_kind == CHECK_CONSTANT_FLOATING && constant.value.floating == expected[op],
                              "half required constant arithmetic has the source-format result");
            action.context.constant_evaluated = false;
            const CheckActionResult runtime = check_test_action(test, table, action);
            check_test_expect(test, runtime.diagnostic == CHECK_DIAG_NONE && runtime.value.fault == CHECK_FAULT_UNSUPPORTED_CONSTANT,
                              "half runtime arithmetic retains its excess-precision lowering");
        }
        const CheckAction divide = {
            .op = CHECK_OP_DIVIDE,
            .operands = { check_test_float(formats[format], 1), check_test_float(formats[format], 3) },
            .context = { .constant_evaluated = true },
        };
        const CheckActionResult quotient = check_test_action(test, table, divide);
        const long double rounded_third = format == 0 ? 0x1.554p-2L : 0x1.56p-2L;
        check_test_expect(test, quotient.diagnostic == CHECK_DIAG_NONE && quotient.value.fault == CHECK_FAULT_NONE &&
                               quotient.value.constant_kind == CHECK_CONSTANT_FLOATING && quotient.value.floating == rounded_third,
                          "half required division rounds one third to the source format in both action paths");
        const long double midpoint = format == 0 ? 2048 : 256;
        CheckAction action = {
            .op = CHECK_OP_ADD,
            .operands = { check_test_float(formats[format], midpoint), check_test_float(formats[format], 1) },
            .context = { .constant_evaluated = true },
        };
        const CheckActionResult rounded = check_test_action(test, table, action);
        action.op = CHECK_OP_SUBTRACT;
        action.operands[0] = rounded.value;
        action.operands[1] = check_test_float(formats[format], midpoint);
        const CheckActionResult difference = check_test_action(test, table, action);
        check_test_expect(test, rounded.value.floating == midpoint && difference.value.fault == CHECK_FAULT_NONE &&
                               difference.value.constant_kind == CHECK_CONSTANT_FLOATING && difference.value.floating == 0,
                          "half required constant chains round every operation instead of preserving float excess precision");
        action.op = CHECK_OP_MULTIPLY;
        action.operands[0] = check_test_float(formats[format], 0);
        action.operands[1] = check_test_float(formats[format], -1);
        const CheckActionResult zero = check_test_action(test, table, action);
        check_test_expect(test, zero.value.fault == CHECK_FAULT_NONE && zero.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                               zero.value.floating == 0 && signbit(zero.value.floating),
                          "half required constant multiplication preserves negative zero");
        const long double tiny = format == 0 ? 0x1p-24L : 0x1p-133L;
        action.op = CHECK_OP_ADD;
        action.operands[0] = check_test_float(formats[format], tiny);
        action.operands[1] = action.operands[0];
        const CheckActionResult subnormal = check_test_action(test, table, action);
        check_test_expect(test, subnormal.value.fault == CHECK_FAULT_NONE && subnormal.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                               subnormal.value.floating == 2 * tiny,
                          "half required constant arithmetic retains subnormal results");
        action.op = CHECK_OP_MULTIPLY;
        action.operands[0] = check_test_float(formats[format], format == 0 ? 65504 : 0x1.fep127L);
        action.operands[1] = check_test_float(formats[format], 2);
        const CheckActionResult overflow = check_test_action(test, table, action);
        check_test_expect(test, overflow.value.fault == CHECK_FAULT_NONE && overflow.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                               isinf(overflow.value.floating) && !signbit(overflow.value.floating),
                          "half required constant arithmetic rounds overflow to positive infinity");
        action.op = CHECK_OP_ADD;
        action.operands[0] = check_test_float(formats[format], __builtin_infl());
        action.operands[1] = check_test_float(formats[format], -__builtin_infl());
        const CheckActionResult invalid = check_test_action(test, table, action);
        const CheckHalf half = format == 0 ? CHECK_HALF_IEEE : CHECK_HALF_BRAIN;
        const uint16_t canonical = format == 0 ? 0x7e00u : 0x7fc0u;
        check_test_expect(test, invalid.value.fault == CHECK_FAULT_NONE && invalid.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                               check_action_half_bits(half, invalid.value.floating) == canonical,
                          "half required invalid constant arithmetic makes Clang's positive canonical NaN");
        action.operands[0] = check_test_float(formats[format], -__builtin_nanl(""));
        action.operands[1] = check_test_float(formats[format], __builtin_nanl(""));
        const CheckActionResult propagated = check_test_action(test, table, action);
        check_test_expect(test, propagated.value.fault == CHECK_FAULT_NONE && propagated.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                               check_action_half_bits(half, propagated.value.floating) == (uint16_t)(canonical | 0x8000u),
                          "half required NaN arithmetic preserves the first NaN operand's sign");
    }
}

// A required arithmetic context is independent of the optional const-object receipts. Shared leaves do not
// turn a neighboring runtime chain into a constant, through any graph schedule or transition-cache request.
static void check_test_half_constant_graph(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(262144);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, &storage, 32, 32);
    check_test_expect(test, ready, "half constant graph allocation");
    if (ready)
    {
        const TypeId formats[] = { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_BF16 };
        uint32_t required[2];
        uint32_t runtime[2];
        for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
        {
            const long double midpoint = format == 0 ? 2048 : 256;
            const uint32_t large = check_test_leaf(&graph, 1, check_test_float(formats[format], midpoint));
            const uint32_t one = check_test_leaf(&graph, 2, check_test_float(formats[format], 1));
            const uint32_t constant_sum = check_graph_add(&graph, CHECK_OP_ADD, 3, large, one, BUSTER_CHECK_NONE);
            required[format] = check_graph_add(&graph, CHECK_OP_SUBTRACT, 4, constant_sum, large, BUSTER_CHECK_NONE);
            graph.contexts[required[format]] = BUSTER_CHECK_CONTEXT_STATIC;
            const uint32_t runtime_sum = check_graph_add(&graph, CHECK_OP_ADD, 5, large, one, BUSTER_CHECK_NONE);
            runtime[format] = check_graph_add(&graph, CHECK_OP_SUBTRACT, 6, runtime_sum, large, BUSTER_CHECK_NONE);
        }
        CheckStream scalar;
        const CheckOptions options = { .scalar = true };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &scalar);
        check_test_expect(test, graph.const_values == NULL && checked && scalar.report_count == 0,
                          "required half arithmetic needs no optional const-object receipt table");
        if (checked)
        {
            for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
            {
                const CheckValue constant = check_stream_value(&scalar, required[format]);
                const CheckValue ordinary = check_stream_value(&scalar, runtime[format]);
                check_test_expect(test, constant.fault == CHECK_FAULT_NONE && constant.constant_kind == CHECK_CONSTANT_FLOATING &&
                                       constant.floating == 0 && ordinary.fault == CHECK_FAULT_UNSUPPORTED_CONSTANT,
                                  "required half graph chains round each operation without folding neighboring runtime chains");
            }
            const uint64_t mark = output.length;
            for (uint32_t batch = 0; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                CheckStream vector;
                const CheckOptions partition = { .batch_size = batch, .regions = batch != 0, .transitions = batch == 0 };
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &partition, &vector) &&
                                       check_test_stream_equal(&scalar, &vector, graph.argument_count),
                                  "half required contexts and runtime chains agree across graph schedules and transition requests");
                arena_pop_to(&output, mark);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

// A folded NaN's x87 fields, as the checker's long double holds every real format's NaN (check_action_nan_bits).
static bool check_test_nan_fields(long double value, uint16_t top, uint64_t significand)
{
    uint64_t low = 0;
    uint16_t high = 0;
    memcpy(&low, &value, sizeof(low));
    memcpy(&high, (const uint8_t*)&value + sizeof(low), sizeof(high));
    return isnan(value) && high == top && low == significand;
}

// float, double and long double folds give NaNs as clang's APFloat does (#1473), in required and ordinary contexts
// alike: an invalid operation on non-NaNs gives the positive canonical NaN, where the host's SSE and x87 give their
// negative one, and otherwise the result is the first NaN operand's sign and payload, quieted, whichever operand the
// host's operation would take. The expected fields are written out, independent of the encoder.
static void check_test_real_nan_actions(CheckTest* test, TypeTable* table)
{
    const TypeId formats[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    const long double infinity = __builtin_infl();
    const long double negative = -__builtin_nanl("0x5");
    const long double signaling = __builtin_nansl("0x9");
    const uint64_t canonical = UINT64_C(0xc000000000000000);
    const struct
    {
        CheckOp op;
        long double a;
        long double b;
        uint16_t top;
        uint64_t significand;
    } cases[] =
    {
        { CHECK_OP_ADD, infinity, -infinity, 0x7fff, canonical },
        { CHECK_OP_ADD, -infinity, infinity, 0x7fff, canonical },
        { CHECK_OP_SUBTRACT, infinity, infinity, 0x7fff, canonical },
        { CHECK_OP_SUBTRACT, -infinity, -infinity, 0x7fff, canonical },
        { CHECK_OP_MULTIPLY, 0, infinity, 0x7fff, canonical },
        { CHECK_OP_MULTIPLY, -infinity, -0.0L, 0x7fff, canonical },
        { CHECK_OP_DIVIDE, 0, -0.0L, 0x7fff, canonical },
        { CHECK_OP_DIVIDE, -infinity, infinity, 0x7fff, canonical },
        { CHECK_OP_ADD, 1, negative, 0xffff, canonical | 5 },
        { CHECK_OP_SUBTRACT, 1, negative, 0xffff, canonical | 5 },
        { CHECK_OP_MULTIPLY, negative, 2, 0xffff, canonical | 5 },
        { CHECK_OP_DIVIDE, 2, signaling, 0x7fff, canonical | 9 },
        { CHECK_OP_ADD, signaling, negative, 0x7fff, canonical | 9 },
        { CHECK_OP_MULTIPLY, negative, signaling, 0xffff, canonical | 5 },
        { CHECK_OP_SUBTRACT, negative, __builtin_nanl("0x3"), 0xffff, canonical | 5 },
        { CHECK_OP_DIVIDE, __builtin_nanl("0x3"), negative, 0x7fff, canonical | 3 },
    };
    for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            for (uint32_t required = 0; required < 2; required += 1)
            {
                const CheckAction action = {
                    .op = cases[i].op,
                    .operands = { check_test_float(formats[format], cases[i].a), check_test_float(formats[format], cases[i].b) },
                    .context = { .constant_evaluated = required != 0 },
                };
                const CheckActionResult result = check_test_action(test, table, action);
                check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                                       result.value.constant_kind == CHECK_CONSTANT_FLOATING &&
                                       check_test_nan_fields(result.value.floating, cases[i].top, cases[i].significand),
                                  "a real format's folded NaN has APFloat's sign and payload, not the host's");
            }
        }
    }
}

// A narrower NaN widened to __float128 (#1250): quieted, its sign kept, its payload at the fraction's top, as APFloat
// widens it: the x87 significand's 63 bits below the integer bit become the fraction's bits 111 to 49.
static void check_test_quad_narrow_nan_widening(CheckTest* test, TypeTable* table)
{
    const TypeId formats[] = { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    const CheckOp operations[] = { CHECK_OP_CAST, CHECK_OP_INITIALIZE };
    const long double values[] = { __builtin_nanl("1"), -__builtin_nansl(""), __builtin_nanl("0x1234") };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(formats); i += 1)
    {
        for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(operations); j += 1)
        {
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(values); k += 1)
            {
                const CheckAction action = { .op = operations[j], .operands = { check_test_float(formats[i], values[k]) },
                                             .type = BUSTER_TYPE_FLOAT128 };
                const CheckActionResult result = check_test_action(test, table, action);
                uint64_t significand = 0;
                uint16_t sign_exponent = 0;
                memcpy(&significand, &values[k], sizeof(significand));
                memcpy(&sign_exponent, (const uint8_t*)&values[k] + 8, sizeof(sign_exponent));
                const uint64_t high = ((uint64_t)(sign_exponent & 0x8000u) << 48) | (UINT64_C(0x7fff) << 48) | (UINT64_C(1) << 47) |
                                      ((significand & UINT64_C(0x7fffffffffffffff)) >> 15);
                const uint64_t low = significand << 49;
                check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                                       result.value.constant_kind == CHECK_CONSTANT_FLOATING && (uint64_t)result.value.integer == low &&
                                       (uint64_t)(result.value.integer >> 64) == high && isnan(result.value.floating),
                                  "a narrow NaN widened to __float128 is quiet, its payload at the fraction's top");
            }
        }
    }
}

static __attribute__((noinline)) void check_test_fp_actions(CheckTest* test, TypeTable* table)
{
    check_test_pragma_extensions(test);
    check_test_fp_action_guards(test, table);
    check_test_fp_action_values(test, table);
    check_test_quad_narrow_nan_widening(test, table);
    check_test_half_encodings(test);
    check_test_half_constant_actions(test, table);
    check_test_half_constant_graph(test, table);
    check_test_real_nan_actions(test, table);
    check_test_fp_evaluation_actions(test, table);
    check_test_fp_graph(test, table);
}

#endif
