// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Lazy proof for constant_p under an observable floating environment. Source evaluation, speculative executable
// evaluation and emitted-value constantness are different questions. Only the union of query argument closures
// receives scratch values. A final ordered replay publishes the selected query plans and their ancestors together.
#ifndef BUSTER_C_CHECK_QUERY_RUNTIME_INCLUDED
#define BUSTER_C_CHECK_QUERY_RUNTIME_INCLUDED

typedef struct CheckQueryNumber CheckQueryNumber;
struct CheckQueryNumber
{
    unsigned __int128 magnitude;
    int32_t exponent;
    uint8_t special;
    bool negative;
};

static CheckQueryNumber check_query_normalize(CheckQueryNumber number)
{
    if (number.magnitude != 0 && number.special == 0)
    {
        const uint64_t low = (uint64_t)number.magnitude;
        const uint32_t zeros = low != 0 ? (uint32_t)__builtin_ctzll(low) : 64u + (uint32_t)__builtin_ctzll((uint64_t)(number.magnitude >> 64));
        number.magnitude >>= zeros;
        number.exponent += (int32_t)zeros;
    }
    else if (number.special == 0)
    {
        number.exponent = 0;
        number.negative = false;
    }
    return number;
}

static CheckQueryNumber check_query_float(long double value)
{
    _Static_assert(sizeof(long double) == 16 && LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384, "query proof uses the configured x87 host representation");
    uint64_t significand = 0;
    uint16_t exponent = 0;
    memcpy(&significand, &value, sizeof(significand));
    memcpy(&exponent, (const uint8_t*)&value + 8, sizeof(exponent));
    const uint32_t field = exponent & 0x7fffu;
    CheckQueryNumber number = {
        .magnitude = significand,
        .exponent = (int32_t)(field != 0 ? field : 1) - 16383 - 63,
        .special = field != 0x7fff ? 0 : (significand & UINT64_C(0x7fffffffffffffff)) == 0 ? 1 : (significand & UINT64_C(0x4000000000000000)) != 0 ? 2 : 3,
        .negative = (exponent & 0x8000u) != 0,
    };
    return check_query_normalize(number);
}

static bool check_query_number_equal(CheckQueryNumber a, CheckQueryNumber b)
{
    a = check_query_normalize(a);
    b = check_query_normalize(b);
    return a.special == 0 && b.special == 0 && a.magnitude == b.magnitude && a.exponent == b.exponent && a.negative == b.negative;
}

static bool check_query_sum_exact(CheckQueryNumber a, CheckQueryNumber b, CheckQueryNumber result)
{
    if (a.magnitude == 0)
    {
        return check_query_number_equal(b, result);
    }
    if (b.magnitude == 0)
    {
        return check_query_number_equal(a, result);
    }
    const int32_t exponent = a.exponent < b.exponent ? a.exponent : b.exponent;
    const uint32_t ashift = (uint32_t)(a.exponent - exponent);
    const uint32_t bshift = (uint32_t)(b.exponent - exponent);
    if (ashift >= 128 || bshift >= 128 || check_vector_active_bits(a.magnitude) + ashift > 128 || check_vector_active_bits(b.magnitude) + bshift > 128)
    {
        // Two at-most-64-bit significands separated this far cannot cancel to a target-format result.
        return false;
    }
    const unsigned __int128 left = a.magnitude << ashift;
    const unsigned __int128 right = b.magnitude << bshift;
    CheckQueryNumber sum = { .exponent = exponent, .negative = a.negative };
    if (a.negative == b.negative)
    {
        sum.magnitude = left + right;
        if (sum.magnitude < left)
        {
            return false;
        }
    }
    else
    {
        sum.magnitude = left >= right ? left - right : right - left;
        sum.negative = left >= right ? a.negative : b.negative;
    }
    return check_query_number_equal(sum, result);
}

static bool check_query_float_exact(CheckOp op, long double left, long double right, long double value)
{
    const CheckQueryNumber a = check_query_float(left);
    CheckQueryNumber b = check_query_float(right);
    const CheckQueryNumber result = check_query_float(value);
    if (result.special >= 2)
    {
        // Clang's C constant folder admits its folded NaN result before checking arithmetic operation status.
        return true;
    }
    if (a.special != 0 || b.special != 0 || result.special != 0)
    {
        return (result.special == 1 && (a.special == 1 || b.special == 1)) ||
               (op == CHECK_OP_DIVIDE && a.special == 0 && b.special == 1 && result.magnitude == 0);
    }
    bool exact = true;
    if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        b.negative ^= op == CHECK_OP_SUBTRACT;
        exact = check_query_sum_exact(a, b, result);
    }
    else if (op == CHECK_OP_MULTIPLY)
    {
        const CheckQueryNumber product = { .magnitude = a.magnitude * b.magnitude, .exponent = a.exponent + b.exponent, .negative = a.negative != b.negative };
        exact = check_query_number_equal(product, result);
    }
    else if (op == CHECK_OP_DIVIDE)
    {
        const CheckQueryNumber product = { .magnitude = result.magnitude * b.magnitude, .exponent = result.exponent + b.exponent, .negative = result.negative != b.negative };
        exact = b.magnitude != 0 && check_query_number_equal(a, product);
    }
    return exact;
}

static bool check_query_scalar_conversion_exact(const TypeTable* types, const CheckValue* before, const CheckValue* after)
{
    const uint8_t target = check_action_kind(types, after->type);
    const uint8_t source = check_action_kind(types, before->type);
    bool exact = true;
    if (target == BUSTER_TYPE_KIND_FLOATING && source == BUSTER_TYPE_KIND_FLOATING && before->constant_kind == CHECK_CONSTANT_FLOATING && after->constant_kind == CHECK_CONSTANT_FLOATING)
    {
        const CheckQueryNumber from = check_query_float(before->floating);
        const CheckQueryNumber to = check_query_float(after->floating);
        exact = from.special == 0 ? check_query_number_equal(from, to) : from.special == 1 ? to.special == 1 :
                type_unqualified(before->type) == type_unqualified(after->type) || from.special == 2;
    }
    else if (target == BUSTER_TYPE_KIND_FLOATING && before->constant_kind == CHECK_CONSTANT_INTEGER && after->constant_kind == CHECK_CONSTANT_FLOATING)
    {
        uint32_t width = 0;
        bool sign = false;
        exact = check_action_integer_type(types, before->type, &width, &sign);
        if (exact)
        {
            const unsigned __int128 mask = check_action_mask(width);
            const unsigned __int128 bits = before->integer & mask;
            const bool negative = sign && (bits >> (width - 1)) != 0;
            const CheckQueryNumber integer = { .magnitude = negative ? (0 - bits) & mask : bits, .negative = negative };
            exact = check_query_number_equal(integer, check_query_float(after->floating));
        }
    }
    // C floating-to-integer constant evaluation uses truncation and separately checks its representable range.
    return exact && after->fault == CHECK_FAULT_NONE;
}

static bool check_query_conversion_exact(const TypeTable* types, const CheckValue* before, const CheckValue* after)
{
#if !BUSTER_HOST_VECTOR
    return check_query_scalar_conversion_exact(types, before, after);
#else
    const bool complex_target = check_action_kind(types, after->type) == BUSTER_TYPE_KIND_COMPLEX;
    const bool complex_source = check_action_kind(types, before->type) == BUSTER_TYPE_KIND_COMPLEX;
    const CheckValue real_before = complex_source ? check_complex_component(types, before, false) : *before;
    const CheckValue real_after = complex_target ? check_complex_component(types, after, false) : *after;
    bool exact = check_query_scalar_conversion_exact(types, &real_before, &real_after);
    if (complex_source && complex_target)
    {
        const CheckValue imaginary_before = check_complex_component(types, before, true);
        const CheckValue imaginary_after = check_complex_component(types, after, true);
        exact &= check_query_scalar_conversion_exact(types, &imaginary_before, &imaginary_after);
    }
    return exact;
#endif
}

typedef struct CheckQueryProof CheckQueryProof;
struct CheckQueryProof
{
    CheckValue values[3];
    bool valid[3];
    bool syntax_effects;
    uint8_t evaluated;
    uint32_t source_warnings;
};

static uint32_t check_query_effect_index(const void* records, uint32_t count, size_t stride, uint32_t node)
{
    uint32_t first = 0;
    uint32_t last = count;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        uint32_t at = 0;
        memcpy(&at, (const uint8_t*)records + (size_t)middle * stride, sizeof(at));
        if (at < node)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    return first;
}

static CheckValue check_query_convert(TypeTable* types, const CheckAction* action, CheckValue value, CheckConversion conversion, bool speculative)
{
    if ((conversion.flags & (BUSTER_CHECK_CONVERT_LVALUE | BUSTER_CHECK_CONVERT_ARRAY | BUSTER_CHECK_CONVERT_FUNCTION)) != 0)
    {
        CheckAction operand = *action;
        operand.operands[0] = value;
        CheckActionResult temporary = { 0 };
        value = check_action_operand(types, &operand, &temporary, 0);
    }
    if (conversion.type != 0 && conversion.type != value.type)
    {
        check_action_convert_value(types, &value, conversion.type, action->node);
    }
    return value;
}

static bool check_query_read_effect(const TypeTable* types, const CheckValue* value, CheckConversion conversion)
{
    return (conversion.flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 &&
           ((type_qualifiers(value->type) & BUSTER_TYPE_VOLATILE) != 0 || (value->flags & BUSTER_CHECK_VOLATILE_PART) != 0 ||
            check_action_kind(types, value->type) == BUSTER_TYPE_KIND_ATOMIC);
}

static bool check_query_syntax(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream,
                               const uint32_t* indices, const CheckQueryProof* proofs, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const CBuiltinId builtin = (CBuiltinId)(graph->members != NULL ? graph->members[node] : 0);
    bool effect = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_ASSIGN || op == CHECK_OP_BUILTIN_OVERFLOW ||
                  op == CHECK_OP_VARARGS || op == CHECK_OP_BUILTIN_ATOMIC || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    effect |= op == CHECK_OP_VALUE && (check_graph_leaf_of(graph, node)->flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
    const CheckRegionProof* region = op == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, node) : NULL;
    effect |= op == CHECK_OP_STATEMENT_VALUE && region == NULL && ((graph->members != NULL ? graph->members[node] : 0) & BUSTER_CHECK_SIDE_EFFECT) != 0;
    if (region != NULL)
    {
        effect |= (region->flags & CHECK_REGION_UNSUPPORTED) != 0;
        for (uint32_t i = 0; i < region->count; i += 1)
        {
            const CheckRegionProofStep* step = graph->region_steps + region->first + i;
            if (step->node != BUSTER_CHECK_NONE)
            {
                const CheckValue checked_value = check_stream_value(stream, step->node);
                const CheckValue* value = &checked_value;
                effect |= proofs[indices[step->node]].syntax_effects ||
                          (value->category == CHECK_LVALUE && ((type_qualifiers(value->type) & BUSTER_TYPE_VOLATILE) != 0 ||
                           (value->flags & BUSTER_CHECK_VOLATILE_PART) != 0 || check_action_kind(types, value->type) == BUSTER_TYPE_KIND_ATOMIC));
                effect |= graph->ops[step->node] == CHECK_OP_INITIALIZE && graph->types != NULL &&
                          ((type_qualifiers(graph->types[step->node]) & BUSTER_TYPE_VOLATILE) != 0 ||
                           check_action_kind(types, graph->types[step->node]) == BUSTER_TYPE_KIND_ATOMIC);
            }
        }
    }
    effect |= op == CHECK_OP_BUILTIN_MISC && (builtin == CBUILTIN_UNREACHABLE || builtin == CBUILTIN_TRAP || builtin == CBUILTIN_PREFETCH ||
              builtin == CBUILTIN_MEMCPY || builtin == CBUILTIN_MEMSET || builtin == CBUILTIN_MEMMOVE || builtin == CBUILTIN_ALLOCA ||
              cbuiltin_carry(builtin));
    effect |= op == CHECK_OP_BUILTIN_VECTOR && (builtin == CBUILTIN_NONTEMPORAL_STORE || builtin == CBUILTIN_MASKED_STORE ||
                                                builtin == CBUILTIN_MASKED_COMPRESS_STORE || builtin == CBUILTIN_MASKED_SCATTER);
    effect |= op == CHECK_OP_BUILTIN_FLOAT && check_builtin_math_kind(builtin) == CHECK_BUILTIN_MATH_SQRT;
    const bool unevaluated = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_BUILTIN_QUERY ||
                             (op == CHECK_OP_BUILTIN_FLOAT && cbuiltin_float_nan(builtin)) ||
                             (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_NONDETERMINISTIC_VALUE);
    if (!unevaluated)
    {
        const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (operands[i] != BUSTER_CHECK_NONE)
            {
                const CheckValue operand = check_stream_value(stream, operands[i]);
                effect |= proofs[indices[operands[i]]].syntax_effects || check_query_read_effect(types, &operand, stream->conversions[node].operands[i]);
            }
        }
        if (check_graph_has_arguments(op))
        {
            uint32_t count = graph->argument_counts[node];
            if (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_SHUFFLEVECTOR && count > 2)
            {
                count = 2;
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t slot = graph->argument_offsets[node] + i;
                const uint32_t child = graph->argument_nodes[slot];
                const CheckValue argument = check_stream_value(stream, child);
                effect |= proofs[indices[child]].syntax_effects || check_query_read_effect(types, &argument, stream->argument_conversions[slot]);
            }
        }
    }
    return effect;
}

static uint32_t check_query_operand_mask(const TypeTable* types, const CheckAction* action)
{
    uint32_t mask = (1u << check_action_operand_count(action)) - 1;
    bool truth = false;
    if (action->op == CHECK_OP_SIZEOF || action->op == CHECK_OP_ALIGNOF)
    {
        mask = 0;
    }
    else if (check_action_truth(types, action->operands, &truth))
    {
        if ((action->op == CHECK_OP_LOGICAL_AND && !truth) || (action->op == CHECK_OP_LOGICAL_OR && truth))
        {
            mask = 1;
        }
        else if (action->op == CHECK_OP_CONDITIONAL)
        {
            mask = truth ? 3 : 5;
        }
    }
    return mask;
}

static bool check_query_lane(TypeTable* types, const CheckGraph* graph, const CheckStream* stream, const uint32_t* indices,
                             CheckQueryProof* proofs, uint32_t node, uint32_t lane, CheckValue* arguments, CheckConversion* conversions,
                             Arena* scratch, uint64_t* scratch_peak)
{
    CheckQueryProof* proof = proofs + indices[node];
    CheckAction action;
    check_graph_action(graph, stream, node, arguments, &action);
    const CheckRegionProof* region = action.op == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, node) : NULL;
    if (region != NULL)
    {
        action.member = 0;
    }
    action.argument_conversions = conversions;
    const bool strict = lane != 0 && action.context.fp_environment != 0;
    action.context.fp_environment = 0;
    if (lane != 2)
    {
        action.context.extensions |= BUSTER_CHECK_EXTENSION_GNU_CONSTANT;
    }
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (operands[i] != BUSTER_CHECK_NONE)
        {
            action.operands[i] = proofs[indices[operands[i]]].values[lane];
        }
        action.operands[i].flags &= ~BUSTER_CHECK_FP_ENVIRONMENT;
    }
    for (uint32_t i = 0; i < action.argument_count; i += 1)
    {
        arguments[i] = proofs[indices[graph->argument_nodes[graph->argument_offsets[node] + i]]].values[lane];
        arguments[i].flags &= ~BUSTER_CHECK_FP_ENVIRONMENT;
        conversions[i] = (CheckConversion){ 0 };
    }
    CheckActionResult result = { 0 };
    uint32_t argument_mask = action.argument_count >= 32 ? UINT32_MAX : (1u << action.argument_count) - 1;
    bool builtin_fp_effect = strict && action.op == CHECK_OP_BUILTIN_FLOAT && check_builtin_float_strict(action.member);
    if (action.op == CHECK_OP_BUILTIN_MISC || action.op == CHECK_OP_BUILTIN_FLOAT || action.op == CHECK_OP_BUILTIN_BIT_CAST)
    {
        CheckBuiltinMiscEffect misc;
        CheckBuiltinFloatEffect floating;
        CheckBuiltinBitCastEffect bit_cast;
        CheckStream temporary = { .misc = &misc, .floating = &floating, .bit_casts = &bit_cast };
        check_graph_special_action(types, graph, &temporary, &action, &result);
        argument_mask = action.op == CHECK_OP_BUILTIN_MISC ? (lane != 2 ? misc.constant_evaluated_operands : misc.evaluated_operands) :
                        action.op == CHECK_OP_BUILTIN_FLOAT ? floating.evaluated_operands : bit_cast.evaluated_operands;
        if (action.op == CHECK_OP_BUILTIN_MISC && lane != 2)
        {
            result.value = misc.constant_value;
        }
        if (action.op == CHECK_OP_BUILTIN_FLOAT)
        {
            builtin_fp_effect |= floating.reads_fp_environment || floating.may_raise_fp_exceptions;
            if (builtin_fp_effect)
            {
                // Floating-environment work is not Clang AST HasSideEffects. Keep actual C operand effects
                // separate so an outer query may still prove its emitted integer answer constant.
                result.value.flags &= ~BUSTER_CHECK_SIDE_EFFECT;
                for (uint32_t i = 0; i < action.argument_count && i < 32; i += 1)
                {
                    if ((argument_mask & (1u << i)) != 0)
                    {
                        result.value.flags |= arguments[i].flags & BUSTER_CHECK_SIDE_EFFECT;
                    }
                }
            }
        }
    }
    else if (action.op == CHECK_OP_BUILTIN_UNARY)
    {
        const CheckBuiltinAction input = {
            .id = (CBuiltinId)action.member,
            .operands = { action.operands[0] },
            .argument_count = 1,
            .node = node,
            .context = action.context,
        };
        CheckBuiltinResult output;
        check_builtins(types, &input, &output, 1, true);
        result = output.action;
    }
    else if (action.op == CHECK_OP_CALL || action.op == CHECK_OP_BUILTIN_X86 || action.op == CHECK_OP_BUILTIN_ATOMIC ||
             action.op == CHECK_OP_BUILTIN_VECTOR || action.op == CHECK_OP_BUILTIN_OVERFLOW || action.op == CHECK_OP_VARARGS)
    {
        result.value = check_stream_value(stream, node);
        result.value.constant_kind = CHECK_CONSTANT_NONE;
        result.diagnostic = stream->diagnostics[node];
        memcpy(result.conversions, stream->conversions[node].operands, sizeof(result.conversions));
    }
    else
    {
        check_action_scalar(types, &action, &result);
    }
    const uint32_t mask = check_query_operand_mask(types, &action);
    bool valid = result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                 (result.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    bool exact = true;
    bool constrained = false;
    bool fp_effect = builtin_fp_effect;
    CheckValue converted[3] = { 0 };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (operands[i] != BUSTER_CHECK_NONE && (mask & (1u << i)) != 0)
        {
            converted[i] = check_query_convert(types, &action, action.operands[i], result.conversions[i], lane != 2);
            const CheckQueryProof* child = proofs + indices[operands[i]];
            valid &= child->valid[lane] && (converted[i].constant_kind != CHECK_CONSTANT_NONE || check_action_kind(types, converted[i].type) == BUSTER_TYPE_KIND_VOID);
            exact &= check_query_conversion_exact(types, action.operands + i, converted + i);
            constrained |= strict && result.conversions[i].type != 0 && check_action_fp_conversion(types, action.operands[i].type, result.conversions[i].type);
            fp_effect |= (child->values[lane].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0;
        }
    }
    for (uint32_t i = 0; i < action.argument_count; i += 1)
    {
        if (i < 32 && (argument_mask & (1u << i)) != 0)
        {
            const CheckValue value = check_query_convert(types, &action, arguments[i], conversions[i], lane != 2);
            const CheckQueryProof* child = proofs + indices[graph->argument_nodes[graph->argument_offsets[node] + i]];
            valid &= child->valid[lane] && value.constant_kind != CHECK_CONSTANT_NONE;
            exact &= check_query_conversion_exact(types, arguments + i, &value);
            constrained |= strict && conversions[i].type != 0 && check_action_fp_conversion(types, arguments[i].type, conversions[i].type);
            fp_effect |= (child->values[lane].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0;
        }
    }
    const bool binary_real = check_action_kind(types, converted[0].type) == BUSTER_TYPE_KIND_FLOATING &&
                             (action.op == CHECK_OP_ADD || action.op == CHECK_OP_SUBTRACT || action.op == CHECK_OP_MULTIPLY || action.op == CHECK_OP_DIVIDE);
    if (binary_real && converted[0].constant_kind == CHECK_CONSTANT_FLOATING && converted[1].constant_kind == CHECK_CONSTANT_FLOATING &&
        result.value.constant_kind == CHECK_CONSTANT_FLOATING)
    {
        exact &= check_query_float_exact(action.op, converted[0].floating, converted[1].floating, result.value.floating);
    }
    if (strict)
    {
        if (action.op >= CHECK_OP_LESS && action.op <= CHECK_OP_NOT_EQUAL &&
            converted[0].constant_kind == CHECK_CONSTANT_FLOATING && converted[1].constant_kind == CHECK_CONSTANT_FLOATING)
        {
            const uint8_t left = check_query_float(converted[0].floating).special;
            const uint8_t right = check_query_float(converted[1].floating).special;
            exact &= left < 2 && right < 2;
        }
        const bool floating_truth = (action.op == CHECK_OP_NOT || action.op == CHECK_OP_CONDITION || action.op == CHECK_OP_CONDITIONAL) &&
                                    check_action_floating_type(types, action.operands[0].type);
        constrained |= binary_real || floating_truth || ((action.op >= CHECK_OP_LESS && action.op <= CHECK_OP_LOGICAL_OR) &&
                        (check_action_floating_type(types, action.operands[0].type) || check_action_floating_type(types, action.operands[1].type)));
    }
    if (lane == 1 && strict && !exact)
    {
        valid = false;
    }
    if (region != NULL)
    {
        const CheckRegionValues values = { .values = proofs[0].values + lane, .valid = proofs[0].valid + lane,
                                          .indices = indices, .stride = sizeof(CheckQueryProof) };
        CheckRegionResult prefix;
        if (!check_region_walk(types, graph, region, &values, lane == 2, false, scratch, scratch_peak, &prefix))
        {
            return false;
        }
        check_action_inherit(&result.value, &prefix.effects);
        valid &= prefix.constant;
        fp_effect |= (prefix.effects.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0;
    }
    if (lane == 2)
    {
        // Emitting a constrained instruction produces an SSA value even when its operands happen to be exact.
        if (constrained || region != NULL)
        {
            result.value.constant_kind = CHECK_CONSTANT_NONE;
        }
        result.value.flags = (result.value.flags & ~BUSTER_CHECK_FP_ENVIRONMENT) |
                             (fp_effect || constrained ? BUSTER_CHECK_FP_ENVIRONMENT : 0);
        valid = result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                (result.value.constant_kind != CHECK_CONSTANT_NONE || check_action_kind(types, result.value.type) == BUSTER_TYPE_KIND_VOID);
    }
    else if (!valid)
    {
        result.value.constant_kind = CHECK_CONSTANT_NONE;
    }
    proof->values[lane] = result.value;
    if (lane == 0)
    {
        proof->source_warnings = result.warnings;
    }
    proof->valid[lane] = valid && (result.value.constant_kind != CHECK_CONSTANT_NONE || check_action_kind(types, result.value.type) == BUSTER_TYPE_KIND_VOID);
    return result.diagnostic != CHECK_DIAG_RESOURCE;
}


static void check_query_prove(TypeTable* types, const CheckGraph* graph, const CheckStream* stream, const uint32_t* indices,
                              CheckQueryProof* proofs, uint32_t node)
{
    CheckQueryProof* proof = proofs + indices[node];
    const uint32_t child_node = graph->argument_nodes[graph->argument_offsets[node]];
    const CheckQueryProof* child = proofs + indices[child_node];
    const CheckBuiltinCall* witness = check_graph_builtin_call(graph, node);
    const bool syntax = child->syntax_effects || (witness != NULL && witness->constant_p_side_effects);
    CheckBuiltinQueryAction input = { .id = CBUILTIN_CONSTANT_P, .argument_count = 1, .node = node,
        .string_literal_start = witness != NULL && witness->string_literal_start,
        .absolute_address_constant = witness != NULL && witness->absolute_address_constant };
    bool folded[3] = { false, false, false };
    for (uint32_t lane = 0; lane < 3; lane += 1)
    {
        const CheckValue value = check_query_convert(types, &(CheckAction){ .node = node, .context = check_graph_context(graph, node, false) },
                                                   child->values[lane], stream->argument_conversions[graph->argument_offsets[node]], lane != 2);
        input.complex_constant = value.constant_kind == CHECK_CONSTANT_COMPLEX;
        folded[lane] = child->valid[lane] && check_builtin_query_constant(types, &input, &value);
        proof->values[lane] = check_stream_value(stream, node);
        proof->values[lane].constant_kind = CHECK_CONSTANT_INTEGER;
        proof->values[lane].integer = folded[lane];
        proof->values[lane].flags &= ~BUSTER_CHECK_FP_ENVIRONMENT;
        proof->valid[lane] = lane != 1 || folded[1] || syntax;
    }
    if (!proof->valid[1])
    {
        proof->values[1].constant_kind = CHECK_CONSTANT_NONE;
    }
    const uint8_t kind = check_action_kind(types, child->values[2].type);
    const bool numeric = check_action_is_integer(types, child->values[2].type) || kind == BUSTER_TYPE_KIND_FLOATING;
    proof->evaluated = !proof->valid[1] && !syntax && numeric;
    if (child->values[2].fault != CHECK_FAULT_NONE && (child->values[2].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0)
    {
        // A pure invalid constant emits poison, not a CPU operation. is.constant(poison) answers false.
        proof->evaluated = 0;
    }
    proof->values[2].integer = proof->valid[1] ? proof->values[1].integer : proof->evaluated && folded[2];
    proof->values[2].flags = proof->evaluated ? child->values[2].flags & BUSTER_CHECK_FP_ENVIRONMENT : 0;
    const bool differs = proof->values[0].integer != proof->values[2].integer;
    proof->values[0].flags |= (differs || (proof->values[2].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0) ? BUSTER_CHECK_FP_ENVIRONMENT : 0;
}

static bool check_graph_query_runtime(TypeTable* types, const CheckGraph* graph, CheckStream* stream, Arena* scratch,
                                      uint8_t* marked, uint32_t* indices, CheckValue* arguments, uint32_t maximum_arguments, bool scalar)
{
    (void)scalar;

    bool needed = false;
    for (uint32_t i = 0; i < stream->query_count; i += 1)
    {
        const uint32_t node = stream->queries[i].node;
        if ((graph->members != NULL ? graph->members[node] : 0) == CBUILTIN_CONSTANT_P && graph->argument_counts[node] == 1)
        {
            needed = true;
        }
    }
    if (!needed)
    {
        return true;
    }
    memset(marked, 0, graph->count);
    for (uint32_t i = 0; i < stream->query_count; i += 1)
    {
        marked[stream->queries[i].node] = 1;
    }
    for (uint32_t end = graph->count; end != 0; end -= 1)
    {
        const uint32_t node = end - 1;
        if (marked[node] != 0)
        {
            const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < 3; i += 1)
            {
                if (operands[i] != BUSTER_CHECK_NONE)
                {
                    marked[operands[i]] = 1;
                }
            }
            if (check_graph_has_arguments(graph->ops[node]))
            {
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    marked[graph->argument_nodes[graph->argument_offsets[node] + i]] = 1;
                }
            }
            const CheckRegionProof* region = graph->ops[node] == CHECK_OP_STATEMENT_VALUE ? check_graph_region_proof(graph, node) : NULL;
            for (uint32_t i = 0; region != NULL && i < region->count; i += 1)
            {
                const CheckRegionProofStep* step = graph->region_steps + region->first + i;
                if (step->node != BUSTER_CHECK_NONE)
                {
                    marked[step->node] = 1;
                }
                if (step->operand != BUSTER_CHECK_NONE)
                {
                    marked[step->operand] = 1;
                }
            }
        }
    }
    uint32_t count = 0;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        indices[node] = marked[node] != 0 ? count++ : BUSTER_CHECK_NONE;
    }
    CheckQueryProof* proofs = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckQueryProof, count);
    CheckConversion* conversions = maximum_arguments != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckConversion, maximum_arguments) : NULL;
    if (proofs == NULL || (maximum_arguments != 0 && conversions == NULL))
    {
        stream->status = CHECK_STATUS_RESOURCE;
        return false;
    }
    memset(proofs, 0, (size_t)count * sizeof(*proofs));
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        if (marked[node] != 0)
        {
            CheckQueryProof* proof = proofs + indices[node];
            proof->syntax_effects = check_query_syntax(types, graph, stream, indices, proofs, node);
            if (graph->ops[node] == CHECK_OP_BUILTIN_QUERY && (graph->members != NULL ? graph->members[node] : 0) == CBUILTIN_CONSTANT_P && graph->argument_counts[node] == 1 && stream->diagnostics[node] == CHECK_DIAG_NONE)
            {
                check_query_prove(types, graph, stream, indices, proofs, node);
            }
            else if (graph->ops[node] == CHECK_OP_BUILTIN_QUERY)
            {
                const uint32_t index = check_query_effect_index(stream->queries, stream->query_count, sizeof(*stream->queries), node);
                for (uint32_t lane = 0; lane < 3; lane += 1)
                {
                    proof->values[lane] = lane == 2 ? stream->queries[index].runtime_value : check_stream_value(stream, node);
                    proof->valid[lane] = check_action_foldable(proof->values + lane);
                }
            }
            else
            {
                for (uint32_t lane = 0; lane < 3; lane += 1)
                {
                    if (!check_query_lane(types, graph, stream, indices, proofs, node, lane, arguments, conversions, scratch, &stream->statistics.scratch_bytes))
                    {
                        stream->status = CHECK_STATUS_RESOURCE;
                        stream->failed_node = node;
                        return false;
                    }
                }
            }
        }
    }
    const CheckStatistics statistics = stream->statistics;
    if (stream->statistics.scratch_bytes < scratch->length)
    {
        stream->statistics.scratch_bytes = scratch->length;
    }
    stream->effect_count = 0;
    stream->varargs_count = 0;
    stream->misc_count = 0;
    stream->floating_count = 0;
    stream->query_count = 0;
    stream->atomic_count = 0;
    stream->vector_count = 0;
    stream->x86_count = 0;
    stream->bit_cast_count = 0;
    stream->overload_count = 0;
    stream->overload_address_count = 0;
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        check_graph_scalar_node(types, graph, stream, node, arguments, scratch, BUSTER_SCALAR_PATH(scalar));
        if (marked[node] != 0)
        {
            stream->warnings[node] |= proofs[indices[node]].source_warnings;
        }
        if (graph->ops[node] == CHECK_OP_BUILTIN_QUERY && (graph->members != NULL ? graph->members[node] : 0) == CBUILTIN_CONSTANT_P && graph->argument_counts[node] == 1 && stream->diagnostics[node] == CHECK_DIAG_NONE)
        {
            const CheckQueryProof* proof = proofs + indices[node];
            const CheckQueryProof* child = proofs + indices[graph->argument_nodes[graph->argument_offsets[node]]];
            const CheckBuiltinCall* witness = check_graph_builtin_call(graph, node);
            const CheckBuiltinQueryAction input = {
                .id = CBUILTIN_CONSTANT_P,
                .arguments = arguments,
                .argument_count = 1,
                .node = node,
                .context = check_graph_context(graph, node, false),
                .string_literal_start = witness != NULL && witness->string_literal_start,
                .absolute_address_constant = witness != NULL && witness->absolute_address_constant,
                .complex_constant = child->values[0].constant_kind == CHECK_CONSTANT_COMPLEX,
                .constant_p_source_proof = proof->values[0].integer != 0 ? CHECK_BUILTIN_QUERY_PROOF_ONE : CHECK_BUILTIN_QUERY_PROOF_ZERO,
                .constant_p_runtime_plan = proof->evaluated != 0 ? CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN : CHECK_BUILTIN_QUERY_RUNTIME_SKIP,
                .constant_p_runtime_result = (uint8_t)proof->values[2].integer,
                .constant_p_runtime_effects = {
                    .flags = child->values[2].flags & BUSTER_CHECK_SIDE_EFFECT,
                    .fault = child->values[2].fault,
                    .fault_node = child->values[2].fault != CHECK_FAULT_NONE ? child->values[2].fault_node : 0,
                    .reads_fp_environment = (child->values[2].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0,
                    .may_raise_fp_exceptions = (child->values[2].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0,
                },
                .constant_p_context_sensitive = true,
            };
            CheckBuiltinQueryResult checked;
            check_builtin_query(types, &input, &checked, 1);
            checked.action.value.flags |= proof->values[0].flags & BUSTER_CHECK_FP_ENVIRONMENT;
            check_graph_store(stream, node, &checked.action);
            CheckBuiltinQueryEffect* effect = stream->queries + stream->query_count - 1;
            effect->runtime_value = checked.runtime_value;
            effect->evaluated_operands = checked.evaluated_operands;
            effect->source_proof = checked.source_proof;
            effect->runtime_plan = checked.runtime_plan;
            effect->runtime_effects = checked.runtime_effects;
            effect->unsupported = checked.unsupported;
            effect->known = checked.known;
        }
    }
    const uint64_t scratch_peak = stream->statistics.scratch_bytes;
    stream->statistics = statistics;
    stream->statistics.scratch_bytes = scratch_peak;
    stream->statistics.query_proof_nodes += (uint64_t)count * 3;
    stream->statistics.query_replay_nodes += graph->count;
    return true;
}

#endif
