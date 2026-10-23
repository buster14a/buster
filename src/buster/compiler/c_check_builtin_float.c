// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed floating builtin actions (#148), included after c_types.c, c_builtin_catalog.c and c_check_actions.c.
// No action reads an input program's pointer. NAN payload metadata is the entire decoded narrow StringLiteral
// after parens/casts, excluding its final terminator; embedded NUL remains in the slice and prevents folding.
// The producer must not label a known pointer/array or a string-plus-offset as an eligible literal.
//
// Actions/results and each arguments slice are bounded, ordinarily aligned and disjoint from output/type storage;
// no padding is readable. Empty batches accept null pointers. Payload bytes need no alignment or final terminator.
// An eligible empty payload permits a null bytes pointer; unknown payload metadata is never accessed. Batches preserve source order while interning decay
// types. No storage/work survives a call, so legal partitions produce identical semantic output.
//
// fpclassify has an explicit evaluation contract: pure constant evaluation visits only the selected selector.
// If that fails, Clang's runtime path evaluates value5, ZERO4, conditional NAN0, conditional INF1, then both
// NORMAL2/SUBNORMAL3. A runtime consumer must retain that control flow; it cannot flatten the possible-operand
// mask into eager argument evaluation. Unsupported real floating formats remain typed runtime actions and are
// never approximated through host long double. These actions grant no source/selector capability by themselves.
// FABS/FABSF/FABSL use their declared FP prototype conversion before clearing the sign. Unlike the integer
// classifiers, a folded floating call is neither an ICE nor a floating literal, including in an unevaluated arm.
#ifndef BUSTER_C_CHECK_BUILTIN_FLOAT_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_FLOAT_INCLUDED

#include <float.h>

#define BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_FORMAT 0x01u
#define BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_COMPLEX_SELECTOR 0x02u
#define BUSTER_CHECK_BUILTIN_FLOAT_UNKNOWN_PAYLOAD 0x04u
#define BUSTER_CHECK_BUILTIN_FLOAT_INVALID_PAYLOAD 0x08u
#define BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_CONVERSION 0x10u
#define BUSTER_CHECK_BUILTIN_FLOAT_WARN_QUALIFIERS 0x01u
#define BUSTER_CHECK_BUILTIN_FLOAT_WARN_POINTER_SIGN 0x02u
#define BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE 0x04u
#define BUSTER_CHECK_BUILTIN_FLOAT_NO_SELECTOR UINT32_MAX

typedef enum CheckBuiltinFloatEvaluation
{
    CHECK_BUILTIN_FLOAT_EVALUATION_ORDINARY,
    CHECK_BUILTIN_FLOAT_EVALUATION_FOLDED_SELECTOR,
    CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME,
} CheckBuiltinFloatEvaluation;

typedef struct CheckBuiltinFloatAction CheckBuiltinFloatAction;
struct CheckBuiltinFloatAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    const uint8_t* nan_payload;
    uint32_t nan_payload_length;
    bool nan_payload_known;
};

typedef struct CheckBuiltinFloatResult CheckBuiltinFloatResult;
struct CheckBuiltinFloatResult
{
    CheckActionResult action;
    CheckConversion argument_conversions[6];
    CheckValue selectors[5];
    TypeId floating_type;
    // Exact for ordinary/folded/known-class actions; possible operands for an unknown runtime fpclassify class.
    uint32_t evaluated_operands;
    uint32_t definite_operands;
    uint32_t selected_selector;
    uint32_t unsupported;
    uint32_t warnings;
    CBuiltinId runtime_builtin;
    CheckBuiltinFloatEvaluation evaluation;
    bool folded;
    bool reads_memory;
};

static bool check_builtin_float_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinFloatAction* input)
{
    const TypeId base = type_strip_aligned(table, type);
    bool complete = type_is_complete(table, base);
    const uint8_t kind = type_kind(table, base);
    if (complete && input->tag_completions != NULL &&
        (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM))
    {
        const uint32_t tag = table->types.index[type_index(base)];
        complete = tag < input->tag_completion_count && input->tag_completions[tag] <= input->source_position;
    }
    return complete;
}

static CheckValue check_builtin_float_argument(TypeTable* table, const CheckBuiltinFloatAction* input, CheckBuiltinFloatResult* output, uint32_t index)
{
    CheckValue value = input->arguments[index];
    CheckActionResult converted = { 0 };
    if (!type_is_valid(table, value.type) || value.category > CHECK_FUNCTION)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
    }
    else
    {
        const TypeId base = type_strip_aligned(table, value.type);
        const uint8_t kind = type_kind(table, base);
        const TypeId loaded = type_kind_is_array(kind) ? type_element(table, base) : base;
        const bool needs_complete = value.category == CHECK_LVALUE || kind == BUSTER_TYPE_KIND_ENUM;
        if (needs_complete && !check_builtin_float_source_complete(table, loaded, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, index);
        }
        else
        {
            const CheckAction action = {
                .operands = { value },
                .node = input->node,
                .tag_completions = input->tag_completions,
                .tag_completion_count = input->tag_completion_count,
                .source_position = input->source_position,
                .extensions = input->extensions,
            };
            value = check_action_operand(table, &action, &converted, 0);
            if (converted.diagnostic != CHECK_DIAG_NONE)
            {
                check_action_diagnose(&output->action, converted.diagnostic, index);
            }
        }
    }
    output->argument_conversions[index] = converted.conversions[0];
    return value;
}

static bool check_builtin_float_payload(const uint8_t* bytes, uint32_t length, uint64_t* payload)
{
    uint32_t base = 10;
    uint32_t first = 0;
    bool valid = length == 0 || bytes != NULL;
    *payload = 0;
    if (valid && length >= 2 && bytes[0] == '0')
    {
        if (bytes[1] == 'x' || bytes[1] == 'X')
        {
            base = 16;
            first = 2;
        }
        else if (bytes[1] == 'b' || bytes[1] == 'B')
        {
            base = 2;
            first = 2;
        }
        else if (bytes[1] == 'o')
        {
            base = 8;
            first = 2;
        }
        else if (bytes[1] >= '0' && bytes[1] <= '9')
        {
            base = 8;
            first = 1;
        }
        valid = first < length;
    }
    for (uint32_t i = first; valid && i < length; i += 1)
    {
        const uint8_t byte = bytes[i];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 :
                               byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 : UINT32_MAX;
        valid = digit < base;
        // Arbitrarily long valid numbers are reduced modulo 2^62, never rejected for numeric overflow.
        *payload = (*payload * base + (valid ? digit : 0)) & UINT64_C(0x3fffffffffffffff);
    }
    return valid;
}

static long double check_builtin_float_special(TypeId type, bool nan, uint64_t payload)
{
    _Static_assert(sizeof(long double) == 16 && LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384, "the checker host uses x87 extended precision");
    long double result = 0;
    if (type == BUSTER_TYPE_FLOAT)
    {
        const uint32_t bits = UINT32_C(0x7f800000) | (nan ? UINT32_C(0x00400000) | ((uint32_t)payload & UINT32_C(0x003fffff)) : 0);
        float value;
        memcpy(&value, &bits, sizeof(value));
        result = value;
    }
    else if (type == BUSTER_TYPE_DOUBLE)
    {
        const uint64_t bits = UINT64_C(0x7ff0000000000000) | (nan ? UINT64_C(0x0008000000000000) | (payload & UINT64_C(0x0007ffffffffffff)) : 0);
        double value;
        memcpy(&value, &bits, sizeof(value));
        result = value;
    }
    else
    {
        uint8_t bytes[16] = { 0 };
        const uint64_t significand = UINT64_C(0x8000000000000000) | (nan ? UINT64_C(0x4000000000000000) | payload : 0);
        const uint16_t exponent = 0x7fff;
        memcpy(bytes, &significand, sizeof(significand));
        memcpy(bytes + 8, &exponent, sizeof(exponent));
        memcpy(&result, bytes, sizeof(result));
    }
    return result;
}

static uint32_t check_builtin_float_class(TypeId type, long double value)
{
    // CheckValue already represents its declared source format; classifying it introduces no conversion.
    const long double number = value;
    uint32_t result;
    if (isnan(number))
    {
        result = 0;
    }
    else if (isinf(number))
    {
        result = 1;
    }
    else if (number == 0)
    {
        result = 4;
    }
    else
    {
        const long double minimum = type == BUSTER_TYPE_FLOAT ? 0x1p-126L : type == BUSTER_TYPE_DOUBLE ? 0x1p-1022L : 0x1p-16382L;
        result = fabsl(number) < minimum ? 3 : 2;
    }
    return result;
}

static bool check_builtin_float_absolute_warning(const TypeTable* table, TypeId source, TypeId parameter)
{
    const TypeId plain = type_unqualified(type_strip_aligned(table, source));
    const uint8_t kind = type_kind(table, plain);
    bool warning = false;
    if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        uint64_t rank = 0;
        uint64_t width = 0;
        bool is_signed = false;
        type_integer_properties(table, plain, &rank, &width, &is_signed);
        warning = !is_signed || type_size(table, plain) <= type_size(table, BUSTER_TYPE_LLONG);
    }
    else if (kind == BUSTER_TYPE_KIND_ENUM)
    {
        const TypeId underlying = type_unqualified(type_strip_aligned(table, table->types.base[type_index(plain)]));
        uint64_t rank = 0;
        uint64_t width = 0;
        bool is_signed = false;
        type_integer_properties(table, underlying, &rank, &width, &is_signed);
        warning = !is_signed || type_size(table, plain) <= type_size(table, BUSTER_TYPE_LLONG);
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        warning = type_size(table, plain) > type_size(table, parameter);
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        // Clang warns only when one of its cabs variants has enough source storage, up through complex long double.
        warning = type_size(table, plain) <= 2 * type_size(table, BUSTER_TYPE_LONG_DOUBLE);
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind))
    {
        warning = true;
    }
    return warning;
}

static void check_builtin_float_absolute_bits(const long double* value, long double* output)
{
    // CheckValue's supported FP formats are represented in host x87 extended precision. Clearing the sign alone
    // preserves quiet/signaling state and payload; a numeric operation must not turn an sNaN into a qNaN here.
    uint8_t bytes[16];
    memcpy(bytes, value, sizeof(bytes));
    bytes[9] &= 0x7f;
    memcpy(output, bytes, sizeof(bytes));
}

static void check_builtin_float_inherit(CheckBuiltinFloatResult* output, const CheckValue* values, uint32_t possible, uint32_t definite, uint32_t count)
{
    output->evaluated_operands = possible;
    output->definite_operands = definite;
    const uint32_t classifier_order[] = { 5, 4, 0, 1, 2, 3 };
    for (uint32_t at = 0; at < count; at += 1)
    {
        const uint32_t i = count == 6 ? classifier_order[at] : at;
        if ((possible & (UINT32_C(1) << i)) != 0)
        {
            output->action.value.flags |= values[i].flags & BUSTER_CHECK_SIDE_EFFECT;
        }
        if ((definite & (UINT32_C(1) << i)) != 0)
        {
            check_action_inherit(&output->action.value, &values[i]);
        }
    }
}

static void check_builtin_float_nan_argument(TypeTable* table, const CheckBuiltinFloatAction* input, CheckBuiltinFloatResult* output, CheckValue* value)
{
    const TypeId parameter = type_pointer(table, type_qualify(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST));
    if (parameter == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
    }
    else
    {
        const CheckAction context = {
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        bool compatible = check_action_assignable(table, parameter, value, &context, &output->action);
        if ((output->action.warnings & CHECK_WARNING_DISCARDED_QUALIFIERS) != 0)
        {
            output->warnings |= BUSTER_CHECK_BUILTIN_FLOAT_WARN_QUALIFIERS;
        }
        const TypeId source = type_unqualified(type_strip_aligned(table, value->type));
        if (!compatible && check_action_kind(table, source) == BUSTER_TYPE_KIND_POINTER)
        {
            const TypeId from = type_strip_aligned(table, type_element(table, source));
            const TypeId plain = type_unqualified(from);
            const bool character = plain == BUSTER_TYPE_CHAR || plain == BUSTER_TYPE_SCHAR || plain == BUSTER_TYPE_UCHAR;
            if (character || check_action_kind(table, from) == BUSTER_TYPE_KIND_VOID)
            {
                compatible = true;
                if ((type_qualifiers(from) & ~BUSTER_TYPE_CONST) != 0)
                {
                    output->warnings |= BUSTER_CHECK_BUILTIN_FLOAT_WARN_QUALIFIERS;
                }
                if (character && plain != BUSTER_TYPE_CHAR)
                {
                    output->warnings |= BUSTER_CHECK_BUILTIN_FLOAT_WARN_POINTER_SIGN;
                }
            }
        }
        if (!compatible)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else
        {
            check_action_conversion(table, value, parameter, BUSTER_CHECK_CONVERT_ASSIGNMENT, &output->argument_conversions[0], input->node);
        }
    }
}

static void check_builtin_float_one(TypeTable* table, const CheckBuiltinFloatAction* input, CheckBuiltinFloatResult* output)
{
    *output = (CheckBuiltinFloatResult){
        .selected_selector = BUSTER_CHECK_BUILTIN_FLOAT_NO_SELECTOR,
    };
    const bool special = input->id >= CBUILTIN_HUGE_VAL && input->id <= CBUILTIN_INFL;
    const bool nan = input->id >= CBUILTIN_NAN && input->id <= CBUILTIN_NANL;
    const bool classify = input->id >= CBUILTIN_ISNAN && input->id <= CBUILTIN_ISINF_SIGN;
    const bool absolute = input->id == CBUILTIN_FABS || input->id == CBUILTIN_FABSF || input->id == CBUILTIN_FABSL;
    const bool selectors = input->id == CBUILTIN_FPCLASSIFY;
    const uint32_t count = special ? 0 : selectors ? 6 : 1;
    if (!special && !nan && !classify && !absolute)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != count)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (count != 0 && input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        TypeId result_type = BUSTER_TYPE_DOUBLE;
        if (input->id == CBUILTIN_HUGE_VALF || input->id == CBUILTIN_INFF || input->id == CBUILTIN_NANF || input->id == CBUILTIN_FABSF)
        {
            result_type = BUSTER_TYPE_FLOAT;
        }
        else if (input->id == CBUILTIN_HUGE_VALL || input->id == CBUILTIN_INFL || input->id == CBUILTIN_NANL || input->id == CBUILTIN_FABSL)
        {
            result_type = BUSTER_TYPE_LONG_DOUBLE;
        }
        output->action.value.type = classify ? BUSTER_TYPE_INT : result_type;
        output->action.value.category = CHECK_RVALUE;
        output->runtime_builtin = input->id;
        CheckValue values[6] = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            values[i] = check_builtin_float_argument(table, input, output, i);
        }
        if (special)
        {
            output->action.value.constant_kind = CHECK_CONSTANT_FLOATING;
            output->action.value.floating = check_builtin_float_special(result_type, false, 0);
            output->floating_type = result_type;
            output->folded = true;
        }
        else if (nan && output->action.diagnostic == CHECK_DIAG_NONE)
        {
            check_builtin_float_nan_argument(table, input, output, &values[0]);
            check_builtin_float_inherit(output, values, 1, 1, count);
            output->floating_type = result_type;
            output->reads_memory = true;
            uint64_t payload = 0;
            if (!input->nan_payload_known)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_FLOAT_UNKNOWN_PAYLOAD;
            }
            else if (input->nan_payload_length != 0 && input->nan_payload == NULL)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else if (!check_builtin_float_payload(input->nan_payload, input->nan_payload_length, &payload))
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_FLOAT_INVALID_PAYLOAD;
            }
            else if (output->action.value.fault == CHECK_FAULT_NONE)
            {
                output->action.value.constant_kind = CHECK_CONSTANT_FLOATING;
                output->action.value.floating = check_builtin_float_special(result_type, true, payload);
                output->folded = (output->action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
                output->reads_memory = !output->folded;
            }
        }
        else if (absolute && output->action.diagnostic == CHECK_DIAG_NONE)
        {
            if (check_builtin_float_absolute_warning(table, input->arguments[0].type, result_type))
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE;
            }
            if (!check_action_is_arithmetic(table, values[0].type))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
            }
            else
            {
                const uint8_t kind = check_action_kind(table, values[0].type);
                uint32_t width = 0;
                bool is_signed = false;
                const bool wide_integer = check_action_is_integer(table, values[0].type) && !check_action_integer_type(table, values[0].type, &width, &is_signed);
                if (kind == BUSTER_TYPE_KIND_COMPLEX || (kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, values[0].type)) || wide_integer)
                {
                    output->unsupported |= BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_CONVERSION;
                    values[0].constant_kind = CHECK_CONSTANT_NONE;
                }
                if (type_unqualified(type_strip_aligned(table, values[0].type)) == result_type)
                {
                    // A same-format prototype conversion is an identity. Do not round through host FP arithmetic,
                    // which can quiet a signaling NaN even though fabs itself only clears the sign bit.
                    output->argument_conversions[0].type = result_type;
                    output->argument_conversions[0].flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT;
                    values[0].type = result_type;
                }
                else
                {
                    check_action_conversion(table, &values[0], result_type, BUSTER_CHECK_CONVERT_ASSIGNMENT, &output->argument_conversions[0], input->node);
                }
                output->floating_type = result_type;
                check_builtin_float_inherit(output, values, 1, 1, count);
                if (values[0].constant_kind == CHECK_CONSTANT_FLOATING && output->action.value.fault == CHECK_FAULT_NONE)
                {
                    output->action.value.constant_kind = CHECK_CONSTANT_FLOATING;
                    check_builtin_float_absolute_bits(&values[0].floating, &output->action.value.floating);
                    output->folded = (output->action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
                }
            }
        }
        else if (classify && output->action.diagnostic == CHECK_DIAG_NONE)
        {
            const uint32_t fp = selectors ? 5 : 0;
            const TypeId type = type_unqualified(type_strip_aligned(table, values[fp].type));
            output->floating_type = type;
            if (check_action_kind(table, type) != BUSTER_TYPE_KIND_FLOATING)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, fp);
            }
            const bool supported = check_action_float_supported(table, type);
            if (!supported)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_FORMAT;
            }
            for (uint32_t i = 0; selectors && i < 5; i += 1)
            {
                if (!check_action_is_arithmetic(table, values[i].type))
                {
                    check_action_diagnose(&output->action, CHECK_DIAG_ARITHMETIC_REQUIRED, i);
                }
                else
                {
                    const uint8_t kind = check_action_kind(table, values[i].type);
                    if (kind == BUSTER_TYPE_KIND_COMPLEX || (kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, values[i].type)))
                    {
                        output->unsupported |= kind == BUSTER_TYPE_KIND_COMPLEX ? BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_COMPLEX_SELECTOR : BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_FORMAT;
                        values[i].constant_kind = CHECK_CONSTANT_NONE;
                    }
                    check_action_conversion(table, &values[i], BUSTER_TYPE_INT, BUSTER_CHECK_CONVERT_ASSIGNMENT, &output->argument_conversions[i], input->node);
                }
                output->selectors[i] = values[i];
            }
            const bool known = supported && values[fp].constant_kind == CHECK_CONSTANT_FLOATING && values[fp].fault == CHECK_FAULT_NONE;
            const uint32_t category = known ? check_builtin_float_class(type, values[fp].floating) : BUSTER_CHECK_BUILTIN_FLOAT_NO_SELECTOR;
            if (!selectors)
            {
                check_builtin_float_inherit(output, values, 1, 1, count);
                if (known)
                {
                    const uint64_t answer = input->id == CBUILTIN_ISNAN ? category == 0 : input->id == CBUILTIN_ISINF ? category == 1 :
                                            input->id == CBUILTIN_ISFINITE ? category != 0 && category != 1 : input->id == CBUILTIN_ISINF_SIGN ?
                                            (category == 1 ? (signbit(values[0].floating) ? UINT32_MAX : 1) : 0) : !!signbit(values[0].floating);
                    output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
                    output->action.value.integer = answer;
                    output->folded = (output->action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
                }
            }
            else
            {
                output->selected_selector = category;
                const bool selected_constant = known && values[category].constant_kind == CHECK_CONSTANT_INTEGER && values[category].fault == CHECK_FAULT_NONE;
                const bool pure = selected_constant && ((values[category].flags | values[fp].flags) & BUSTER_CHECK_SIDE_EFFECT) == 0;
                if (pure)
                {
                    output->evaluation = CHECK_BUILTIN_FLOAT_EVALUATION_FOLDED_SELECTOR;
                    const uint32_t mask = (UINT32_C(1) << fp) | (UINT32_C(1) << category);
                    check_builtin_float_inherit(output, values, mask, mask, count);
                    output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
                    output->action.value.integer = values[category].integer;
                    output->unsupported = 0;
                    output->folded = true;
                }
                else
                {
                    output->evaluation = CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME;
                    const uint32_t mask = !known || category == 2 || category == 3 ? 63 : category == 4 ? 48 : category == 0 ? 49 : 51;
                    check_builtin_float_inherit(output, values, mask, known ? mask : 48, count);
                    if (selected_constant && output->action.value.fault == CHECK_FAULT_NONE)
                    {
                        output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
                        output->action.value.integer = values[category].integer;
                    }
                }
            }
        }
        for (uint32_t i = 0; i < count && i < 3; i += 1)
        {
            output->action.conversions[i] = output->argument_conversions[i];
        }
        if (output->folded)
        {
            output->runtime_builtin = CBUILTIN_NONE;
            if (classify)
            {
                output->action.value.flags |= BUSTER_CHECK_ICE;
            }
        }
    }
    if (output->action.diagnostic != CHECK_DIAG_NONE)
    {
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
        output->folded = false;
    }
}

static void check_builtin_float(TypeTable* table, const CheckBuiltinFloatAction* actions, CheckBuiltinFloatResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_float_one(table, actions + i, results + i);
    }
}

#endif
