// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared-context and provenance regression tests (#1058). Include after c_check_const_values_test.c. Expected
// values and effects are stated independently of the scalar/vector comparison. Interleaved views deliberately
// reuse object ids; neither a previous lane, a previous call nor a future source cut may supply the next value.
#ifndef BUSTER_C_CHECK_CONTEXT_TEST_INCLUDED
#define BUSTER_C_CHECK_CONTEXT_TEST_INCLUDED

static bool check_test_context_equal(CheckContext a, CheckContext b)
{
    const bool equal = a.tag_completions == b.tag_completions && a.transparent_unions == b.transparent_unions &&
                       a.tag_completion_count == b.tag_completion_count && a.source_position == b.source_position &&
                       a.extensions == b.extensions && a.fp_environment == b.fp_environment &&
                       a.const_values == b.const_values && a.constant_evaluated == b.constant_evaluated;
    return equal;
}

static void check_test_context_roots(CheckTest* test, TypeTable* types)
{
    uint32_t completions[] = { 11, 22 };
    uint32_t transparent[] = { 13, 24 };
    uint32_t positions[] = { 37 };
    uint32_t tokens[] = { 1 };
    uint32_t operands[] = { BUSTER_CHECK_NONE };
    uint8_t systems[] = { 0, 1 };
    uint8_t fp[] = { BUSTER_CHECK_FP_ENV_ACCESS };
    uint8_t constant[] = { 1 };
    CheckOp ops[] = { CHECK_OP_SIZEOF };
    TypeId queried[] = { BUSTER_TYPE_INT };
    CheckConstValues view = { 0 };
    CheckGraph graph = {
        .count = 1,
        .ops = ops,
        .types = queried,
        .operand0 = operands,
        .operand1 = operands,
        .operand2 = operands,
        .tokens = tokens,
        .source_positions = positions,
        .tag_completions = completions,
        .transparent_unions = transparent,
        .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
        .extensions = BUSTER_CHECK_EXTENSION_GNU,
        .fp_environments = fp,
        .system_tokens = systems,
        .system_token_count = BUSTER_ARRAY_LENGTH(systems),
        .const_values = &view,
    };
    CheckStream stream = { .constant_evaluated = constant };
    CheckAction action;
    check_graph_action(&graph, &stream, 0, NULL, &action);
    CheckContext expected = {
        .tag_completions = completions,
        .transparent_unions = transparent,
        .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
        .source_position = 37,
        .extensions = BUSTER_CHECK_EXTENSION_GNU | BUSTER_CHECK_EXTENSION_SYSTEM_HEADER,
        .fp_environment = BUSTER_CHECK_FP_ENV_ACCESS,
        .const_values = &view,
        .constant_evaluated = true,
    };
    check_test_expect(test, action.node == 0 && check_test_context_equal(action.context, expected), "graph constructs all context independently of diagnostic identity");
    const CheckOverloadContext overload = check_graph_overload_context(&action, &stream);
    const CheckAction adapted = check_overload_action(overload);
    check_test_expect(test, check_test_context_equal(adapted.context, expected) && overload.scratch_peak == &stream.statistics.scratch_bytes,
                      "overload round trip retains every semantic field without treating accounting as context");
    constant[0] = 0;
    check_graph_action(&graph, &stream, 0, NULL, &action);
    expected.constant_evaluated = false;
    check_test_expect(test, check_test_context_equal(action.context, expected), "graph's runtime view retains evidence without authorizing a read");
    CheckInitWalker walker = { .graph = &graph, .const_values = &view, .source_position = 71, .ignore_excess = true };
    expected.source_position = 71;
    expected.constant_evaluated = true;
    check_test_expect(test, check_test_context_equal(check_init_context(&walker, 0, 1), expected), "initializer keeps its own completion cut and the graph's other policy");
    walker.const_values = NULL;
    expected.const_values = NULL;
    expected.constant_evaluated = false;
    check_test_expect(test, check_test_context_equal(check_init_context(&walker, 0, 1), expected), "runtime initializer cannot borrow permission from the graph's receipt view");
    Parser parser = { 0 };
    parser.language.gnu = true;
    CheckInput input = { .graph = graph };
    CheckInputReader reader = { .parser = &parser, .types = types, .output = &input, .tag_completions = completions, .transparent_unions = transparent };
    expected.tag_completion_count = types->tags.capacity;
    expected.source_position = 83;
    expected.extensions |= BUSTER_CHECK_EXTENSION_GNU_CONSTANT;
    expected.fp_environment = 0;
    expected.const_values = &input.const_values;
    expected.constant_evaluated = true;
    check_test_expect(test, check_test_context_equal(check_input_context(&reader, 83, 1, true), expected), "declaration root supplies one source/system/receipt contract to every family");
}

static void check_test_context_histories(CheckTest* test, TypeTable* types)
{
    const TypeId record = type_tag(types, BUSTER_TYPE_KIND_STRUCT, 190001);
    const TypeMember member = { .name = 190002, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    const bool complete = record != 0 && type_complete_record(types, record, &member, 1, (TypeRecordAttributes){ 0 });
    Arena storage = arena_reserve(65536);
    uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(&storage, uint32_t, types->tags.count);
    check_test_expect(test, complete && positions != NULL, "shared visibility fixture storage");
    if (complete && positions != NULL)
    {
        for (uint32_t i = 0; i < types->tags.count; i += 1)
        {
            positions[i] = 42;
        }
        const TypeId array = type_aligned(types, type_array(types, type_array(types, record, 2), 3), 5);
        CheckContext context = { .tag_completions = positions, .tag_completion_count = types->tags.count, .source_position = 41 };
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            context.source_position = 41 + pass;
            const bool visible = pass != 0;
            CheckAction action = { .op = CHECK_OP_SIZEOF, .type = array, .node = visible ? 0 : UINT32_MAX, .context = context };
            const CheckActionResult result = check_test_action(test, types, action);
            check_test_expect(test, (result.diagnostic == CHECK_DIAG_NONE) == visible &&
                                   (!visible || result.value.integer == type_size(types, array)), "nested aligned arrays use source completion, never diagnostic node order");
            const CheckBuiltinAction integer = { .context = context };
            const CheckBuiltinMiscAction misc = { .context = context };
            const CheckBuiltinFloatAction floating = { .context = context };
            const CheckBuiltinAtomicAction atomic = { .context = context };
            const CheckVarargsAction varargs = { .context = context };
            const CheckBuiltinVectorAction vector = { .context = context };
            check_test_expect(test, check_builtin_source_complete(types, record, &integer) == visible &&
                                   check_builtin_misc_source_complete(types, record, &misc) == visible &&
                                   check_builtin_float_source_complete(types, record, &floating) == visible &&
                                   check_builtin_atomic_source_complete(types, record, &atomic) == visible &&
                                   check_varargs_source_complete(types, record, &varargs) == visible &&
                                   check_builtin_vector_complete(types, record, &vector) == visible,
                              "all source-completeness consumers share the same nominal visibility boundary");
        }
        context.tag_completion_count = 0;
        check_test_expect(test, !check_context_source_complete(types, array, &context), "an empty supplied history cannot read an unbounded tag entry");
        context.tag_completions = NULL;
        check_test_expect(test, check_context_source_complete(types, array, &context), "a null history deliberately selects direct-call current-table semantics");
    }
    arena_release(&storage);
}

// A source default and an explicit lexical off are different contracts. Exercise newly appended nodes and both
// validators on empty graphs, full vector blocks and partial blocks; malformed policies must never enable fusion.
static void check_test_context_contract(CheckTest* test)
{
    enum { CAPACITY = 35 };
    Arena storage = arena_reserve(UINT64_C(1) << 28);
    CheckGraph graph = { 0 };
    const bool ready = storage.base != NULL && check_graph_initialize(&graph, &storage, CAPACITY, 0);
    check_test_expect(test, ready, "floating contraction context fixture storage");
    if (ready)
    {
        const uint32_t counts[] = { 0, 1, 15, 16, 17, 32, CAPACITY };
        const uint8_t policies[] = { 0, BUSTER_CHECK_FP_CONTRACT_ON, BUSTER_CHECK_FP_CONTRACT_FAST };
        uint8_t environments[CAPACITY];
        uint8_t pure[CAPACITY + 1]; // The graph validators use one sentinel byte after the nodes.
        for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(counts); size += 1)
        {
            graph.fp_environments = NULL;
            while (graph.count < counts[size])
            {
                check_test_leaf(&graph, graph.count, check_test_integer(BUSTER_TYPE_INT, graph.count));
            }
            for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
            {
                graph.fp_contract_default = policies[policy];
                bool inherited = true;
                for (uint32_t node = 0; node < graph.count; node += 1)
                {
                    const CheckContext context = check_graph_context(&graph, node, false);
                    inherited &= check_graph_fp_contract(&graph, node) == policies[policy] && context.fp_environment == policies[policy];
                }
                check_test_expect(test, inherited, "new nodes inherit off/on/fast without a lexical policy allocation");
                for (uint32_t defined = 0; defined < 2; defined += 1)
                {
                    CheckGraphValidation validation;
                    uint32_t failed;
                    const bool valid = defined ? check_test_graph_validate_definition(&graph, pure, &validation, &failed) :
                                                 check_graph_validate(&graph, pure, &validation, &failed);
                    check_test_expect(test, valid && failed == BUSTER_CHECK_NONE, "both graph validators accept every valid default at node-block boundaries");
                }
            }
            graph.fp_contract_default = BUSTER_CHECK_FP_CONTRACT_FAST;
            graph.fp_environments = environments;
            for (uint32_t node = 0; node < graph.count; node += 1)
            {
                const uint8_t policy = policies[node % BUSTER_ARRAY_LENGTH(policies)];
                environments[node] = policy | BUSTER_CHECK_FP_ENV_ACCESS | BUSTER_CHECK_FP_EVAL_DOUBLE;
                const CheckContext context = check_graph_context(&graph, node, false);
                check_test_expect(test, check_graph_fp_contract(&graph, node) == policy && context.fp_environment == environments[node],
                                  "lexical off/on/fast replaces the default while preserving environment access and evaluation format");
            }
            for (uint32_t defined = 0; defined < 2; defined += 1)
            {
                CheckGraphValidation validation;
                uint32_t failed;
                const bool valid = defined ? check_test_graph_validate_definition(&graph, pure, &validation, &failed) :
                                             check_graph_validate(&graph, pure, &validation, &failed);
                check_test_expect(test, valid, "both graph validators accept independent lexical contraction and floating environment settings");
                if (graph.count != 0)
                {
                    const uint32_t node = graph.count - 1;
                    const uint8_t saved = environments[node];
                    environments[node] |= BUSTER_CHECK_FP_CONTRACT_MASK;
                    const bool invalid = defined ? check_test_graph_validate_definition(&graph, pure, &validation, &failed) :
                                                   check_graph_validate(&graph, pure, &validation, &failed);
                    check_test_expect(test, !invalid && failed == node, "contradictory lexical contraction permissions fail at the exact node");
                    environments[node] = saved;
                }
                const uint8_t invalid_defaults[] = { BUSTER_CHECK_FP_CONTRACT_MASK, BUSTER_CHECK_FP_ENV_ACCESS };
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_defaults); i += 1)
                {
                    graph.fp_contract_default = invalid_defaults[i];
                    const bool invalid = defined ? check_test_graph_validate_definition(&graph, pure, &validation, &failed) :
                                                   check_graph_validate(&graph, pure, &validation, &failed);
                    check_test_expect(test, !invalid, "an invalid graph default is rejected even for an empty graph or a lexical override");
                }
                graph.fp_contract_default = BUSTER_CHECK_FP_CONTRACT_FAST;
            }
        }
    }
    arena_release(&storage);
}

static void check_test_context_receipts(CheckTest* test, TypeTable* types)
{
    Arena first_arena = arena_reserve(UINT64_C(1) << 20);
    Arena second_arena = arena_reserve(UINT64_C(1) << 20);
    CheckConstBuffer first = { .arena = &first_arena };
    CheckConstBuffer second = { .arena = &second_arena };
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const TypeId cv = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
    const TypeId ca = type_qualify(types, type_atomic(types, BUSTER_TYPE_INT), BUSTER_TYPE_CONST);
    bool ready = check_const_test_append_value(&first, types, 1, 10, ci, check_test_integer(BUSTER_TYPE_INT, 5)) &&
                 check_const_test_append_value(&second, types, 1, 10, ci, check_test_integer(BUSTER_TYPE_INT, 8)) &&
                 check_const_test_append_value(&first, types, 2, 10, BUSTER_TYPE_INT, check_test_integer(BUSTER_TYPE_INT, 5)) &&
                 check_const_test_append_value(&first, types, 3, 10, ca, check_test_integer(BUSTER_TYPE_INT, 5));
    check_test_expect(test, ready, "interleaved receipts fixture initialization");
    const CheckConstValues views[] = { check_const_view(&first), check_const_view(&second) };
    if (ready)
    {
        enum { COUNT = 23 };
        CheckBuiltinAction builtins[COUNT];
        CheckBuiltinResult results[COUNT];
        CheckBuiltinResult reference[COUNT];
        for (uint32_t repeat = 0; repeat < 3; repeat += 1)
        {
            for (uint32_t i = 0; i < COUNT; i += 1)
            {
                const uint32_t kind = (i + repeat) % 11;
                const bool known = kind == 0 || kind == 2 || kind == 6;
                const bool effect = kind == 4 || kind == 7;
                const uint32_t value = kind == 2 ? 8 : 5;
                CheckValue operand = { .type = ci, .category = CHECK_LVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 1 };
                operand.type = kind == 4 ? cv : kind == 5 ? BUSTER_TYPE_INT : kind == 6 || kind == 7 ? ca : ci;
                operand.object = kind == 5 ? 2 : kind == 6 || kind == 7 ? 3 : kind == 10 ? 99 : 1;
                operand.flags = kind == 8 ? BUSTER_CHECK_CONST_READ_RESTRICTED : kind == 9 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
                const CheckContext context = {
                    .source_position = kind == 3 ? 9 : 10,
                    .const_values = &views[kind == 2],
                    .constant_evaluated = kind != 1 && kind != 7,
                };
                const CheckAction action = { .op = CHECK_OP_POSITIVE, .operands = { operand }, .context = context };
                const CheckActionResult ordinary = check_test_action(test, types, action);
                check_test_expect(test, ordinary.diagnostic == CHECK_DIAG_NONE && (ordinary.value.constant_kind == CHECK_CONSTANT_INTEGER) == known &&
                                       (!known || (ordinary.value.integer == value && (ordinary.value.flags & BUSTER_CHECK_ICE) == 0)) &&
                                       ((ordinary.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == effect,
                                  "ordinary operand conversion respects this lane's view, cut, permission and access restrictions");
                const CheckBuiltinMiscAction misc = { .arguments = &operand, .argument_count = 1, .context = context };
                CheckBuiltinMiscResult misc_result = { 0 };
                const CheckValue misc_value = check_builtin_misc_argument(types, &misc, &misc_result, 0);
                const CheckBuiltinFloatAction floating = { .arguments = &operand, .argument_count = 1, .context = context };
                CheckBuiltinFloatResult float_result = { 0 };
                const CheckValue float_value = check_builtin_float_argument(types, &floating, &float_result, 0);
                const CheckBuiltinAtomicAction atomic = { .arguments = &operand, .argument_count = 1, .context = context };
                CheckBuiltinAtomicResult atomic_result = { 0 };
                const CheckValue atomic_value = check_builtin_atomic_argument(types, &atomic, &atomic_result, 0);
                const CheckBuiltinVectorAction vector = { .arguments = &operand, .argument_count = 1, .context = context };
                CheckBuiltinVectorResult vector_result = { 0 };
                const CheckValue vector_value = check_builtin_vector_argument(types, &vector, &vector_result, 0, true);
                check_test_expect(test, misc_result.action.diagnostic == CHECK_DIAG_NONE && float_result.action.diagnostic == CHECK_DIAG_NONE && atomic_result.action.diagnostic == CHECK_DIAG_NONE &&
                                       vector_result.action.diagnostic == CHECK_DIAG_NONE &&
                                       check_test_value_equal(&misc_value, &float_value) && check_test_value_equal(&misc_value, &atomic_value) &&
                                       check_test_value_equal(&misc_value, &vector_value) &&
                                       (misc_value.constant_kind == CHECK_CONSTANT_INTEGER) == known && (!known || misc_value.integer == value) &&
                                       ((misc_value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == effect,
                                  "miscellaneous, floating, atomic and vector adapters retain the same full receipt context");
                builtins[i] = (CheckBuiltinAction){ .id = CBUILTIN_POPCOUNT, .argument_count = 1, .operands = { operand }, .context = context };
                check_builtins(types, &builtins[i], &reference[i], 1, true);
                const CheckValue* folded = &reference[i].action.value;
                check_test_expect(test, reference[i].action.diagnostic == CHECK_DIAG_NONE && (folded->constant_kind == CHECK_CONSTANT_INTEGER) == known &&
                                       (!known || (folded->integer == (kind == 2 ? 1 : 2) && (folded->flags & BUSTER_CHECK_ICE) != 0)) &&
                                       ((folded->flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == effect,
                                  "integer builtin certifies only the permitted fold, retaining runtime atomic and volatile effects");
            }
            for (uint32_t partition = 1; partition <= COUNT; partition += 1)
            {
                for (uint32_t begin = 0; begin < COUNT; begin += partition)
                {
                    const uint32_t count = COUNT - begin < partition ? COUNT - begin : partition;
                    check_builtins(types, builtins + begin, results + begin, count, false);
                }
                for (uint32_t i = 0; i < COUNT; i += 1)
                {
                    check_test_expect(test, check_test_action_equal(&reference[i].action, &results[i].action) &&
                                           reference[i].fold_status == results[i].fold_status && reference[i].store == results[i].store,
                                      "interleaved context batches and every tail partition equal independent scalar calls");
                }
            }
        }
    }
    arena_release(&second_arena);
    arena_release(&first_arena);
}

static void check_test_context_provenance(CheckTest* test, TypeTable* types)
{
    CheckValue value = check_test_integer(BUSTER_TYPE_INT, 0);
    value.flags = 0;
    check_test_expect(test, !check_action_integer_constant(&value, 0) && check_action_integer_constant(&value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT) &&
                           check_value_static_initializer(&value, false), "known payload, strict ICE and static-initializer eligibility are separate properties");
    const uint32_t blockers[] = { BUSTER_CHECK_SIDE_EFFECT, BUSTER_CHECK_DISCARDED_UNEVALUABLE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(blockers); i += 1)
    {
        value.flags = BUSTER_CHECK_ICE | blockers[i];
        check_test_expect(test, !check_action_integer_constant(&value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT) && !check_value_static_initializer(&value, false),
                          "an ICE syntax witness cannot erase evaluated effects or an unevaluable discarded operand");
    }
    value.flags = BUSTER_CHECK_ICE | BUSTER_CHECK_LITERAL_ZERO | BUSTER_CHECK_NULL_POINTER;
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        check_test_expect(test, ((check_value_source_flags((CheckOp)op, value.flags) & BUSTER_CHECK_LITERAL_ZERO) != 0) ==
                               (op == CHECK_OP_VALUE || op == CHECK_OP_SELECT), "only source-transparent nodes retain literal-zero spelling");
    }
    check_test_expect(test, (check_value_receipt_flags(&value) & (BUSTER_CHECK_ICE | BUSTER_CHECK_LITERAL_ZERO | BUSTER_CHECK_NULL_POINTER)) == 0,
                      "receipt values cannot forge literal or null-pointer syntax");
    CheckValue left = check_test_integer(BUSTER_TYPE_INT, 7);
    CheckValue right = check_test_integer(BUSTER_TYPE_INT, 9);
    right.flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_ICE_UNEVALUATED;
    right.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    right.fault_node = 43;
    const CheckValue condition = check_test_integer(BUSTER_TYPE_INT, 1);
    value = (CheckValue){ 0 };
    check_action_conditional_provenance(types, &value, BUSTER_TYPE_INT, &condition, &left, &right, true, true);
    check_test_expect(test, value.integer == 7 && value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0,
                      "an unselected arm contributes neither its deferred fault nor its effects");
    check_action_conditional_provenance(types, &value, BUSTER_TYPE_INT, &condition, &left, &right, true, false);
    check_test_expect(test, value.integer == 9 && value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && value.fault_node == 43 &&
                           (value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "the selected arm retains its own fault origin and effects");
    left = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_MODIFICATION | BUSTER_CHECK_SIDE_EFFECT };
    right = check_test_integer(BUSTER_TYPE_INT, 9);
    check_action_comma_provenance(types, &value, &left, &right, true);
    check_test_expect(test, value.integer == 9 && (value.flags & BUSTER_CHECK_DISCARDED_UNEVALUABLE) != 0 &&
                           (value.flags & BUSTER_CHECK_MODIFICATION) == 0 && !check_value_static_initializer(&value, false),
                      "discarded source evaluation cannot certify a known right operand past a modification");
    check_action_comma_provenance(types, &value, &left, &right, false);
    check_test_expect(test, (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION)) == (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION),
                      "a runtime comma keeps the discarded operand's observable modification");
    value = (CheckValue){ .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 1, .flags = BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED };
    check_action_address_provenance(&value, true);
    check_test_expect(test, (value.flags & BUSTER_CHECK_INACTIVE_UNION_READ) == 0 && (value.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 &&
                           (value.flags & BUSTER_CHECK_CONST_READ_RESTRICTED) != 0, "forming an inactive union member's address is not reading it");
    check_action_dereference_provenance(&value);
    check_test_expect(test, (value.flags & BUSTER_CHECK_INACTIVE_UNION_READ) != 0 && (value.flags & BUSTER_CHECK_CONST_READ_RESTRICTED) != 0,
                      "dereference restores union-read evidence without clearing restricted pointer provenance");
}

// A GNU-folded payload remains usable by the existing required-constant policy, but it cannot prove that this
// evaluation was defined (#2900). Selection excludes unexecuted operations; an accepted receipt starts a new read.
static void check_test_context_invalid_constants(CheckTest* test, TypeTable* types)
{
    const CheckContext context = { .extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT, .constant_evaluated = true };
    const CheckAction sources[] = {
        { .op = CHECK_OP_ADD, .operands = { check_test_integer(BUSTER_TYPE_INT, INT32_MAX), check_test_integer(BUSTER_TYPE_INT, 1) }, .context = context },
        { .op = CHECK_OP_SHIFT_LEFT, .operands = { check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_INT, 32) }, .context = context },
        { .op = CHECK_OP_CAST, .type = BUSTER_TYPE_INT, .operands = { check_test_float(BUSTER_TYPE_DOUBLE, 1e99L) }, .context = context },
    };
    const uint32_t expected[] = { UINT32_C(0x80000000), UINT32_C(0x80000000), INT32_MAX };
    CheckValue folded[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        const CheckActionResult result = check_test_action(test, types, sources[i]);
        folded[i] = result.value;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                               result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == expected[i] &&
                               (result.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0 &&
                               check_action_integer_constant(&result.value, context.extensions),
                          "overflow, invalid shifts and saturated conversions retain GNU payloads with an invalid-evaluation witness");
    }
    CheckAction sign_bit = sources[1];
    sign_bit.operands[1] = check_test_integer(BUSTER_TYPE_INT, 31);
    const CheckActionResult shifted = check_test_action(test, types, sign_bit);
    check_test_expect(test, shifted.diagnostic == CHECK_DIAG_NONE && shifted.value.fault == CHECK_FAULT_NONE &&
                           shifted.value.constant_kind == CHECK_CONSTANT_INTEGER && shifted.value.integer == UINT32_C(0x80000000) &&
                           (shifted.value.flags & BUSTER_CHECK_INVALID_CONSTANT) == 0,
                      "shifting a nonnegative value into its sign bit remains a defined constant evaluation");
    const CheckOp ancestors[] = { CHECK_OP_POSITIVE, CHECK_OP_CAST, CHECK_OP_COMMA, CHECK_OP_STATEMENT_VALUE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ancestors); i += 1)
    {
        const CheckAction action = { .op = ancestors[i], .type = BUSTER_TYPE_LONG,
                                     .operands = { folded[0], check_test_integer(BUSTER_TYPE_INT, 4) }, .context = context };
        const CheckActionResult result = check_test_action(test, types, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                               result.value.constant_kind == CHECK_CONSTANT_INTEGER && (result.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0,
                          "unary, cast, discarded comma and statement-value parents retain evaluated invalid operations");
    }
    for (uint32_t logical = 0; logical < 2; logical += 1)
    {
        for (uint32_t evaluates = 0; evaluates < 2; evaluates += 1)
        {
            const CheckAction action = {
                .op = logical == 0 ? CHECK_OP_LOGICAL_AND : CHECK_OP_LOGICAL_OR,
                .operands = { check_test_integer(BUSTER_TYPE_INT, logical == 0 ? evaluates : !evaluates), folded[0] },
                .context = context,
            };
            const CheckActionResult result = check_test_action(test, types, action);
            check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                                   result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == (logical != 0 || evaluates != 0) &&
                                   ((result.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0) == (evaluates != 0),
                              "logical short circuit excludes only the unexecuted invalid right operand");
        }
    }
    for (uint32_t selects = 0; selects < 2; selects += 1)
    {
        const CheckAction action = { .op = CHECK_OP_CONDITIONAL,
                                     .operands = { check_test_integer(BUSTER_TYPE_INT, selects), folded[0], check_test_integer(BUSTER_TYPE_INT, 4) },
                                     .context = context };
        const CheckActionResult result = check_test_action(test, types, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                               result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == (selects != 0 ? expected[0] : 4) &&
                               ((result.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0) == (selects != 0),
                          "a conditional inherits the selected arm's invalid evaluation without inheriting the unselected arm's");
    }
    const CheckAction conditional = { .op = CHECK_OP_CONDITIONAL,
                                      .operands = { folded[0], check_test_integer(BUSTER_TYPE_INT, 4), check_test_integer(BUSTER_TYPE_INT, 5) },
                                      .context = context };
    const CheckActionResult selected = check_test_action(test, types, conditional);
    check_test_expect(test, selected.diagnostic == CHECK_DIAG_NONE && selected.value.integer == 4 &&
                           (selected.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0,
                      "a known conditional result retains an invalid operation in its evaluated condition");

    Arena storage = arena_reserve(65536);
    CheckConstBuffer buffer = { .arena = &storage };
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    bool ready = storage.base != NULL && ci != 0;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(folded); i += 1)
    {
        ready = check_const_test_append_value(&buffer, types, i + 1, 10, ci, folded[i]);
    }
    check_test_expect(test, ready, "accepted invalid-initializer receipt storage");
    const CheckConstValues view = check_const_view(&buffer);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(folded); i += 1)
    {
        CheckContext read_context = context;
        read_context.const_values = &view;
        read_context.source_position = 10;
        const CheckAction load = { .op = CHECK_OP_POSITIVE,
                                   .operands = { { .type = ci, .category = CHECK_LVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = i + 1 } },
                                   .context = read_context };
        const CheckActionResult read = check_test_action(test, types, load);
        check_test_expect(test, read.diagnostic == CHECK_DIAG_NONE && read.value.fault == CHECK_FAULT_NONE &&
                               read.value.constant_kind == CHECK_CONSTANT_INTEGER && read.value.integer == expected[i] &&
                               (read.value.flags & (BUSTER_CHECK_INVALID_CONSTANT | BUSTER_CHECK_ICE)) == 0 &&
                               (read.value.flags & BUSTER_CHECK_GNU_OBJECT_READ) != 0 &&
                               ((read.value.flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0) == (i == 2),
                          "reading an accepted receipt drops initializer execution provenance but retains its stored value and GNU range witness");
        const CheckAction overflow = { .op = i == 2 ? CHECK_OP_ADD : CHECK_OP_SUBTRACT,
                                       .operands = { read.value, check_test_integer(BUSTER_TYPE_INT, 1) }, .context = context };
        const CheckActionResult repeated = check_test_action(test, types, overflow);
        check_test_expect(test, repeated.diagnostic == CHECK_DIAG_NONE && repeated.value.fault == CHECK_FAULT_NONE &&
                               repeated.value.constant_kind == CHECK_CONSTANT_INTEGER &&
                               repeated.value.integer == (i == 2 ? UINT32_C(0x80000000) : INT32_MAX) &&
                               (repeated.value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0,
                          "a new overflowing operation on a stored value introduces a new invalid-evaluation witness");
    }
    arena_release(&storage);
}

static __attribute__((noinline)) void check_test_semantic_context(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    const bool ready = check_test_types_open(&types);
    check_test_expect(test, ready, "semantic context type table");
    if (ready)
    {
        check_test_context_roots(test, &types.table);
        check_test_context_histories(test, &types.table);
        check_test_context_contract(test);
        check_test_context_receipts(test, &types.table);
        check_test_context_provenance(test, &types.table);
        check_test_context_invalid_constants(test, &types.table);
        check_const_test_exhaustion(test, &types.table);
    }
    check_test_types_close(&types);
}

#endif
