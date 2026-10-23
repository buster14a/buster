// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 semantic actions for the 23 observed generic vector builtin spellings. Include after
// c_check_builtin_atomic.c: its ordinary scalar initialization conversions supply pointer diagnostics here,
// never atomic object/order/alignment constraints. This file does not implement #147's ordinary vector operators.
// Each action borrows bounded arguments and immutable source-position tag history. The caller owns all storage;
// actions, results, arguments and mutable type storage must be disjoint and naturally aligned. No padding is read.
// Zero-count batches permit null slices. Type interning occurs in source order, with no work retained across calls.
//
// convertvector has ONE value argument plus destination_type as its separately parsed TYPE operand. The parser
// must enforce its two syntax operands. Shuffle accepts two vectors with a vector mask, or two matching vectors
// followed by ICE indices. Its arbitrary-length index slice is checked directly, never copied into a fixed array.
// evaluated_argument_count describes a prefix: two shuffle vectors, one convert operand, ordinary arity otherwise.
// nondeterministic_value uses its operand TYPE only, so its prefix is zero and no operand effects/faults survive.
//
// No vector constant payload is represented by CheckValue. These actions publish exact source type relations and
// explicit constant_evaluation_unmodeled, never a fabricated constant or catalog/source/runtime capability.
// Unsupported metadata describes a representation gap, not an invalid C type. A source consumer must retain it.
// fp_evaluation_type is 0 for source evaluation, DOUBLE or LONG_DOUBLE for explicit excess-precision evaluation;
// native_half distinguishes Clang's NativeHalfType option. Vector element types are never promoted by these rules.
// Floating arithmetic and numeric conversions conservatively retain FP-environment/exception ordering. A future
// runtime consumer must implement these effects explicitly; neither an ordinary pure call nor these type actions
// establish that consumer. No action reads or changes the host floating environment or the source program's memory.
// Rules, not implementation, from llvmorg-23.1.1 clang/lib/Sema/SemaChecking.cpp (elementwise/reduction/shuffle,
// convert/nontemporal/nondeterministic), SemaExpr.cpp (unary FP and initialization), and CodeGen/CGBuiltin.cpp:
// https://github.com/llvm/llvm-project/tree/llvmorg-23.1.1/clang/lib
#ifndef BUSTER_C_CHECK_BUILTIN_VECTOR_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_VECTOR_INCLUDED

#define BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_INDEX 0x01u
#define BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_CONVERSION 0x02u
#define BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_MEMORY_PAYLOAD 0x04u
#define BUSTER_CHECK_BUILTIN_VECTOR_WARN_DEPRECATED 0x01u
#define BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS 0x02u
#define BUSTER_CHECK_BUILTIN_VECTOR_WARN_POINTER_SIGN 0x04u
#define BUSTER_CHECK_BUILTIN_VECTOR_WARN_FUNCTION_POINTER 0x08u

typedef enum CheckBuiltinVectorConstraint
{
    CHECK_BUILTIN_VECTOR_CONSTRAINT_NONE,
    CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE,
    CHECK_BUILTIN_VECTOR_CONSTRAINT_RANGE,
} CheckBuiltinVectorConstraint;

typedef enum CheckBuiltinVectorLaxConversions
{
    CHECK_BUILTIN_VECTOR_LAX_DEFAULT,
    CHECK_BUILTIN_VECTOR_LAX_NONE,
    CHECK_BUILTIN_VECTOR_LAX_INTEGER,
    CHECK_BUILTIN_VECTOR_LAX_ALL,
} CheckBuiltinVectorLaxConversions;

typedef enum CheckBuiltinVectorStoreConversion
{
    CHECK_BUILTIN_VECTOR_STORE_NONE,
    CHECK_BUILTIN_VECTOR_STORE_ASSIGNMENT,
    CHECK_BUILTIN_VECTOR_STORE_SPLAT,
    CHECK_BUILTIN_VECTOR_STORE_BITCAST,
    CHECK_BUILTIN_VECTOR_STORE_BOOLEAN_VECTOR,
} CheckBuiltinVectorStoreConversion;

typedef struct CheckBuiltinVectorAction CheckBuiltinVectorAction;
struct CheckBuiltinVectorAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    TypeId destination_type;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    TypeId fp_evaluation_type;
    CheckBuiltinVectorLaxConversions lax_vector_conversions;
    bool native_half;
};

typedef struct CheckBuiltinVectorResult CheckBuiltinVectorResult;
struct CheckBuiltinVectorResult
{
    CheckActionResult action;
    CheckValue converted_arguments[3];
    CheckConversion argument_conversions[3];
    TypeId element_type;
    TypeId memory_type;
    uint32_t lane_count;
    uint32_t evaluated_argument_count;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    CheckBuiltinVectorConstraint constraint;
    CheckBuiltinVectorStoreConversion store_conversion;
    bool reads_memory;
    bool writes_memory;
    bool nondeterministic;
    bool constant_evaluation_unmodeled;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    // prepareVectorSplat converts scalar bool true to signed -1 for a non-bool ext-vector element.
    bool bool_splat_signed_true;
};

static TypeId check_builtin_vector_plain(const TypeTable* table, TypeId type)
{
    const TypeId result = type_unqualified(type_strip_aligned(table, type));
    return result;
}

static bool check_builtin_vector_is_vector(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    const bool result = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    return result;
}

static uint32_t check_builtin_vector_lanes(const TypeTable* table, TypeId type)
{
    const TypeId plain = type_strip_aligned(table, type);
    const uint32_t result = check_builtin_vector_is_vector(table, plain) ? (uint32_t)table->types.extent[type_index(plain)] : 0;
    return result;
}

static TypeId check_builtin_vector_element(const TypeTable* table, TypeId type)
{
    const TypeId result = check_builtin_vector_is_vector(table, type) ? type_element(table, type_strip_aligned(table, type)) : type;
    return result;
}

static bool check_builtin_vector_complete(const TypeTable* table, TypeId type, const CheckBuiltinVectorAction* input)
{
    const TypeId plain = type_strip_aligned(table, type);
    const uint8_t kind = type_kind(table, plain);
    bool result = type_is_complete(table, plain);
    if (result && input->tag_completions != NULL &&
        (kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
    {
        const uint32_t tag = table->types.index[type_index(plain)];
        result = tag < input->tag_completion_count && input->tag_completions[tag] <= input->source_position;
    }
    return result;
}

static CheckValue check_builtin_vector_argument(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output, uint32_t index, bool convert)
{
    CheckValue value = input->arguments[index];
    if (!type_is_valid(table, value.type) || value.category > CHECK_FUNCTION)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
    }
    else
    {
        const uint8_t kind = check_action_kind(table, value.type);
        const bool needs_complete = kind == BUSTER_TYPE_KIND_ENUM || (value.category == CHECK_LVALUE && !type_kind_is_array(kind));
        const TypeId element = check_builtin_vector_element(table, value.type);
        if ((needs_complete && !check_builtin_vector_complete(table, value.type, input)) ||
            (check_action_kind(table, element) == BUSTER_TYPE_KIND_ENUM && !check_builtin_vector_complete(table, element, input)))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, index);
        }
        else if (convert)
        {
            const CheckAction action =
            {
                .operands = { value },
                .node = input->node,
            };
            CheckActionResult converted = { 0 };
            value = check_action_operand(table, &action, &converted, 0);
            output->argument_conversions[index] = converted.conversions[0];
            if (converted.diagnostic != CHECK_DIAG_NONE)
            {
                check_action_diagnose(&output->action, converted.diagnostic, index);
            }
            const TypeId converted_element = check_builtin_vector_element(table, value.type);
            if (check_action_kind(table, converted_element) == BUSTER_TYPE_KIND_ENUM && !check_builtin_vector_complete(table, converted_element, input))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, index);
            }
        }
        else
        {
            output->argument_conversions[index].type = value.type;
        }
    }
    return value;
}

static bool check_builtin_vector_same_math_type(const TypeTable* table, TypeId a, TypeId b)
{
    const bool av = check_builtin_vector_is_vector(table, a);
    const bool bv = check_builtin_vector_is_vector(table, b);
    const bool result = av == bv && check_builtin_vector_plain(table, check_builtin_vector_element(table, a)) ==
        check_builtin_vector_plain(table, check_builtin_vector_element(table, b)) &&
        (!av || check_builtin_vector_lanes(table, a) == check_builtin_vector_lanes(table, b));
    return result;
}

static void check_builtin_vector_fp_promote(const TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output, uint32_t index)
{
    CheckValue* value = output->converted_arguments + index;
    const TypeId plain = check_builtin_vector_plain(table, value->type);
    TypeId destination = plain;
    if (check_action_kind(table, plain) == BUSTER_TYPE_KIND_FLOATING)
    {
        if (input->fp_evaluation_type != 0 && plain != BUSTER_TYPE_FLOAT128 &&
            (input->fp_evaluation_type == BUSTER_TYPE_LONG_DOUBLE || plain != BUSTER_TYPE_LONG_DOUBLE))
        {
            destination = input->fp_evaluation_type;
        }
        else if (plain == BUSTER_TYPE_HALF && !input->native_half)
        {
            destination = BUSTER_TYPE_FLOAT;
        }
    }
    if (destination != plain)
    {
        // The relation needs no host numeric conversion. In particular BF16/F16 payloads are not approximated.
        value->type = destination;
        value->constant_kind = CHECK_CONSTANT_NONE;
        output->argument_conversions[index].type = destination;
        output->argument_conversions[index].flags |= BUSTER_CHECK_CONVERT_PROMOTION;
        output->reads_fp_environment = true;
        output->may_raise_fp_exceptions = true;
    }
}

static bool check_builtin_vector_signed_integer(const TypeTable* table, TypeId type)
{
    TypeId plain = check_builtin_vector_plain(table, type);
    if (type_kind(table, plain) == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, plain))
    {
        plain = check_builtin_vector_plain(table, table->types.base[type_index(plain)]);
    }
    const uint8_t kind = type_kind(table, plain);
    bool result = false;
    if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        uint64_t rank = 0;
        uint64_t width = 0;
        type_integer_properties(table, plain, &rank, &width, &result);
    }
    return result;
}

static bool check_builtin_vector_math_element(const TypeTable* table, CBuiltinId id, TypeId type)
{
    const TypeId element = check_builtin_vector_element(table, type);
    const uint8_t kind = check_action_kind(table, element);
    bool result;
    if (id == CBUILTIN_ELEMENTWISE_ABS)
    {
        result = check_builtin_vector_signed_integer(table, element) || kind == BUSTER_TYPE_KIND_FLOATING;
    }
    else if (id == CBUILTIN_ELEMENTWISE_FMA || id == CBUILTIN_ELEMENTWISE_SQRT)
    {
        result = kind == BUSTER_TYPE_KIND_FLOATING;
    }
    else if (id == CBUILTIN_ELEMENTWISE_MAX || id == CBUILTIN_ELEMENTWISE_MIN)
    {
        result = check_builtin_vector_is_vector(table, type) ||
            (check_builtin_vector_plain(table, element) != BUSTER_TYPE_BOOL && kind != BUSTER_TYPE_KIND_ENUM &&
             (check_action_is_integer(table, element) || kind == BUSTER_TYPE_KIND_FLOATING));
    }
    else
    {
        result = check_action_is_integer(table, element);
    }
    return result;
}

static void check_builtin_vector_math(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        if (!type_is_valid(table, input->arguments[i].type) || input->arguments[i].category > CHECK_FUNCTION)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, i);
        }
    }
    for (uint32_t i = 1; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        const TypeId previous = check_builtin_vector_plain(table, input->arguments[i - 1].type);
        const TypeId current = check_builtin_vector_plain(table, input->arguments[i].type);
        if (check_action_kind(table, previous) == BUSTER_TYPE_KIND_ENUM && check_action_kind(table, current) == BUSTER_TYPE_KIND_ENUM && previous != current)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
        }
    }
    for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        output->converted_arguments[i] = check_builtin_vector_argument(table, input, output, i, true);
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            check_builtin_vector_fp_promote(table, input, output, i);
            const TypeId type = output->converted_arguments[i].type;
            if (!check_builtin_vector_math_element(table, input->id, type))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
            }
            else if (i != 0 && !check_builtin_vector_same_math_type(table, output->converted_arguments[0].type, type))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
            }
        }
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        output->action.value.type = output->converted_arguments[0].type;
        if ((input->id == CBUILTIN_ELEMENTWISE_MIN || input->id == CBUILTIN_ELEMENTWISE_MAX) &&
            check_action_kind(table, check_builtin_vector_element(table, output->action.value.type)) == BUSTER_TYPE_KIND_FLOATING)
        {
            output->warnings |= BUSTER_CHECK_BUILTIN_VECTOR_WARN_DEPRECATED;
        }
    }
}

static void check_builtin_vector_reduce(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    output->converted_arguments[0] = check_builtin_vector_argument(table, input, output, 0, true);
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        const TypeId type = output->converted_arguments[0].type;
        if (!check_builtin_vector_is_vector(table, type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else
        {
            const TypeId element = check_builtin_vector_element(table, type);
            if (input->id != CBUILTIN_REDUCE_MIN && input->id != CBUILTIN_REDUCE_MAX && !check_action_is_integer(table, element))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INTEGER_REQUIRED, 0);
            }
            else
            {
                output->action.value.type = element;
                output->element_type = element;
                output->lane_count = check_builtin_vector_lanes(table, type);
            }
        }
    }
}

static void check_builtin_vector_shuffle_index(const TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output, uint32_t index, uint32_t lanes)
{
    const CheckValue* value = input->arguments + index;
    if (!type_is_valid(table, value->type) || value->category > CHECK_FUNCTION)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
    }
    else if (!check_action_is_integer(table, value->type) || (value->flags & BUSTER_CHECK_ICE) == 0 ||
             (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0 ||
             (value->fault != CHECK_FAULT_NONE && value->fault != CHECK_FAULT_UNSUPPORTED_CONSTANT))
    {
        output->constraint = CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE;
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
    }
    else
    {
        uint32_t width = 0;
        bool is_signed = false;
        const bool represented = check_action_integer_type(table, value->type, &width, &is_signed);
        if (!represented || value->constant_kind != CHECK_CONSTANT_INTEGER || value->fault == CHECK_FAULT_UNSUPPORTED_CONSTANT)
        {
            output->unsupported |= BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_INDEX;
        }
        else
        {
            const unsigned __int128 bits = value->integer & check_action_mask(width);
            const bool undef = is_signed && bits == check_action_mask(width);
            if (!undef && ((bits >> 64) != 0 || bits >= (uint64_t)lanes * 2))
            {
                output->constraint = CHECK_BUILTIN_VECTOR_CONSTRAINT_RANGE;
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
            }
        }
    }
}

static void check_builtin_vector_shuffle(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    for (uint32_t i = 0; i < 2 && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        output->converted_arguments[i] = check_builtin_vector_argument(table, input, output, i, true);
        if (output->action.diagnostic == CHECK_DIAG_NONE && !check_builtin_vector_is_vector(table, input->arguments[i].type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
        }
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        const TypeId left = input->arguments[0].type;
        const TypeId right = input->arguments[1].type;
        const uint32_t lanes = check_builtin_vector_lanes(table, left);
        const uint32_t result_lanes = input->argument_count == 2 ? lanes : input->argument_count - 2;
        if (input->argument_count == 2)
        {
            if (!check_action_is_integer(table, check_builtin_vector_element(table, right)) || check_builtin_vector_lanes(table, right) != lanes)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
            }
        }
        else if (check_builtin_vector_plain(table, left) != check_builtin_vector_plain(table, right))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            TypeId result = left;
            if (result_lanes != lanes)
            {
                result = type_vector(table, check_builtin_vector_element(table, left), result_lanes, check_action_kind(table, left) == BUSTER_TYPE_KIND_EXT_VECTOR);
            }
            if (result == 0)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
            }
            else
            {
                output->action.value.type = result;
                for (uint32_t i = 2; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
                {
                    check_builtin_vector_shuffle_index(table, input, output, i, lanes);
                }
            }
        }
    }
}

static void check_builtin_vector_convert(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    output->converted_arguments[0] = check_builtin_vector_argument(table, input, output, 0, true);
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        if (!type_is_valid(table, input->destination_type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 1);
        }
        else if (!check_builtin_vector_is_vector(table, input->arguments[0].type) || !check_builtin_vector_is_vector(table, input->destination_type) ||
                 check_builtin_vector_lanes(table, input->arguments[0].type) != check_builtin_vector_lanes(table, input->destination_type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else if (!check_builtin_vector_complete(table, check_builtin_vector_element(table, input->destination_type), input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 1);
        }
        else
        {
            output->action.value.type = input->destination_type;
        }
    }
}

static bool check_builtin_vector_lax(const TypeTable* table, const CheckBuiltinVectorAction* input, TypeId source, TypeId destination)
{
    // Exact-release LangOptions.def defaults LaxVectorConversions to All. Explicit source options override it.
    const CheckBuiltinVectorLaxConversions policy = input->lax_vector_conversions == CHECK_BUILTIN_VECTOR_LAX_DEFAULT ? CHECK_BUILTIN_VECTOR_LAX_ALL : input->lax_vector_conversions;
    const bool integer = check_action_is_integer(table, check_builtin_vector_element(table, source)) && check_action_is_integer(table, check_builtin_vector_element(table, destination));
    // Sema::areVectorTypesSameSize compares RAW element storage bits times lanes, never padded vector sizeof.
    const uint64_t source_lanes = check_builtin_vector_is_vector(table, source) ? check_builtin_vector_lanes(table, source) : 1;
    const uint64_t destination_lanes = check_builtin_vector_is_vector(table, destination) ? check_builtin_vector_lanes(table, destination) : 1;
    const uint64_t source_bits = type_size(table, check_builtin_vector_element(table, source)) * 8 * source_lanes;
    const uint64_t destination_bits = type_size(table, check_builtin_vector_element(table, destination)) * 8 * destination_lanes;
    const bool result = policy != CHECK_BUILTIN_VECTOR_LAX_NONE && (policy == CHECK_BUILTIN_VECTOR_LAX_ALL || integer) &&
        source_bits == destination_bits &&
        !(!check_builtin_vector_is_vector(table, source) && check_action_kind(table, destination) == BUSTER_TYPE_KIND_EXT_VECTOR) &&
        !(!check_builtin_vector_is_vector(table, destination) && check_action_kind(table, source) == BUSTER_TYPE_KIND_EXT_VECTOR);
    return result;
}

static bool check_builtin_vector_store_compatible(const TypeTable* table, TypeId source, TypeId destination)
{
    // ASTContext::areCompatibleVectorTypes retains element qualifiers; builtin math's relation drops them.
    const bool result = check_builtin_vector_lanes(table, source) == check_builtin_vector_lanes(table, destination) &&
        type_strip_aligned(table, check_builtin_vector_element(table, source)) == type_strip_aligned(table, check_builtin_vector_element(table, destination));
    return result;
}

static void check_builtin_vector_store(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    CheckValue* value = output->converted_arguments;
    const TypeId destination = output->memory_type;
    const TypeId source = check_builtin_vector_plain(table, value->type);
    const bool destination_vector = check_builtin_vector_is_vector(table, destination);
    const bool source_vector = check_builtin_vector_is_vector(table, source);
    bool accepted = source == destination;
    output->store_conversion = CHECK_BUILTIN_VECTOR_STORE_ASSIGNMENT;
    if (destination_vector && check_action_kind(table, source) == BUSTER_TYPE_KIND_COMPLEX)
    {
        // Clang's later CheckImplicitConversion rejects complex-to-vector conversion even after assignment builds a splat.
        accepted = false;
    }
    else if (!accepted && check_action_kind(table, destination) == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        if (check_action_kind(table, source) == BUSTER_TYPE_KIND_EXT_VECTOR)
        {
            accepted = check_builtin_vector_lanes(table, source) == check_builtin_vector_lanes(table, destination) &&
                check_builtin_vector_plain(table, check_builtin_vector_element(table, destination)) == BUSTER_TYPE_BOOL &&
                check_action_is_integer(table, check_builtin_vector_element(table, source));
            output->store_conversion = CHECK_BUILTIN_VECTOR_STORE_BOOLEAN_VECTOR;
        }
        else if (check_action_is_arithmetic(table, source))
        {
            accepted = true;
            output->store_conversion = CHECK_BUILTIN_VECTOR_STORE_SPLAT;
            output->bool_splat_signed_true = source == BUSTER_TYPE_BOOL &&
                check_builtin_vector_plain(table, check_builtin_vector_element(table, destination)) != BUSTER_TYPE_BOOL;
            if (check_action_kind(table, source) == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, source))
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_CONVERSION;
            }
        }
        else if (source_vector)
        {
            accepted = check_builtin_vector_store_compatible(table, source, destination) || check_builtin_vector_lax(table, input, source, destination);
            output->store_conversion = CHECK_BUILTIN_VECTOR_STORE_BITCAST;
        }
    }
    else if (!accepted && (destination_vector || source_vector))
    {
        accepted = destination_vector && source_vector && (check_builtin_vector_store_compatible(table, source, destination) || check_builtin_vector_lax(table, input, source, destination));
        accepted |= !destination_vector && source_vector && check_action_is_scalar(table, destination) &&
            check_builtin_vector_lanes(table, source) == 1 && check_builtin_vector_lax(table, input, source, destination);
        output->store_conversion = CHECK_BUILTIN_VECTOR_STORE_BITCAST;
    }
    else if (!accepted)
    {
        const CheckBuiltinAtomicAction prototype =
        {
            .arguments = input->arguments,
            .argument_count = input->argument_count,
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        CheckBuiltinAtomicResult converted = { 0 };
        converted.argument_conversions[0] = output->argument_conversions[0];
        check_builtin_atomic_convert(table, &prototype, &converted, value, 0, destination);
        output->argument_conversions[0] = converted.argument_conversions[0];
        output->warnings |= (converted.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS) != 0 ? BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS : 0;
        output->warnings |= (converted.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN) != 0 ? BUSTER_CHECK_BUILTIN_VECTOR_WARN_POINTER_SIGN : 0;
        output->warnings |= (converted.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER) != 0 ? BUSTER_CHECK_BUILTIN_VECTOR_WARN_FUNCTION_POINTER : 0;
        output->warning_operands |= converted.warning_operands;
        output->unsupported |= converted.unsupported != 0 ? BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_CONVERSION : 0;
        accepted = converted.action.diagnostic == CHECK_DIAG_NONE;
        if (!accepted)
        {
            check_action_diagnose(&output->action, converted.action.diagnostic, 0);
        }
    }
    if (!accepted)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else
    {
        value->type = destination;
        value->constant_kind = CHECK_CONSTANT_NONE;
        output->argument_conversions[0].type = destination;
        output->argument_conversions[0].flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT;
    }
}

static void check_builtin_vector_nontemporal(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    const bool store = input->id == CBUILTIN_NONTEMPORAL_STORE;
    const uint32_t pointer_index = store ? 1 : 0;
    output->converted_arguments[pointer_index] = check_builtin_vector_argument(table, input, output, pointer_index, true);
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        const TypeId pointer = output->converted_arguments[pointer_index].type;
        if (check_action_kind(table, pointer) != BUSTER_TYPE_KIND_POINTER)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, pointer_index);
        }
        else
        {
            output->memory_type = check_builtin_vector_plain(table, type_element(table, type_strip_aligned(table, pointer)));
            const TypeId payload = output->memory_type;
            const uint8_t kind = check_action_kind(table, payload);
            const bool floating = kind == BUSTER_TYPE_KIND_FLOATING ||
                (kind == BUSTER_TYPE_KIND_COMPLEX && check_action_kind(table, type_element(table, payload)) == BUSTER_TYPE_KIND_FLOATING);
            if (!check_builtin_vector_complete(table, payload, input))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, pointer_index);
            }
            else if (!check_action_is_integer(table, payload) && kind != BUSTER_TYPE_KIND_POINTER && !floating && !check_builtin_vector_is_vector(table, payload))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, pointer_index);
            }
            else if (store)
            {
                output->converted_arguments[0] = check_builtin_vector_argument(table, input, output, 0, true);
                if (output->action.diagnostic == CHECK_DIAG_NONE)
                {
                    check_builtin_vector_store(table, input, output);
                }
                output->action.value.type = BUSTER_TYPE_VOID;
                output->writes_memory = true;
            }
            else
            {
                output->action.value.type = payload;
                output->reads_memory = true;
            }
            if (output->action.diagnostic == CHECK_DIAG_NONE && kind == BUSTER_TYPE_KIND_COMPLEX)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_MEMORY_PAYLOAD;
            }
        }
    }
}

static bool check_builtin_vector_floating(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    const bool result = kind == BUSTER_TYPE_KIND_FLOATING ||
        (kind == BUSTER_TYPE_KIND_COMPLEX && check_action_kind(table, type_element(table, type_strip_aligned(table, type))) == BUSTER_TYPE_KIND_FLOATING);
    return result;
}

static void check_builtin_vector_fp_effects(const TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    bool arithmetic = input->id == CBUILTIN_ELEMENTWISE_FMA || input->id == CBUILTIN_ELEMENTWISE_SQRT;
    if (input->id == CBUILTIN_ELEMENTWISE_MIN || input->id == CBUILTIN_ELEMENTWISE_MAX || input->id == CBUILTIN_REDUCE_MIN || input->id == CBUILTIN_REDUCE_MAX)
    {
        arithmetic = check_builtin_vector_floating(table, output->element_type);
    }
    else if (input->id == CBUILTIN_CONVERTVECTOR)
    {
        const TypeId source = check_builtin_vector_plain(table, check_builtin_vector_element(table, input->arguments[0].type));
        const TypeId destination = check_builtin_vector_plain(table, output->element_type);
        arithmetic = source != destination && (check_builtin_vector_floating(table, source) || check_builtin_vector_floating(table, destination));
    }
    else if (input->id == CBUILTIN_NONTEMPORAL_STORE &&
             (output->store_conversion == CHECK_BUILTIN_VECTOR_STORE_ASSIGNMENT || output->store_conversion == CHECK_BUILTIN_VECTOR_STORE_SPLAT))
    {
        const TypeId source = check_builtin_vector_plain(table, check_builtin_vector_element(table, type_conversion_operand(table, input->arguments[0].type)));
        const TypeId destination = check_builtin_vector_plain(table, check_builtin_vector_element(table, output->memory_type));
        arithmetic = source != destination && (check_builtin_vector_floating(table, source) || check_builtin_vector_floating(table, destination));
    }
    output->reads_fp_environment |= arithmetic;
    output->may_raise_fp_exceptions |= arithmetic;
}

static __attribute__((unused)) void check_builtin_vector_one(TypeTable* table, const CheckBuiltinVectorAction* input, CheckBuiltinVectorResult* output)
{
    *output = (CheckBuiltinVectorResult){ 0 };
    const bool recognized = input->id >= CBUILTIN_ELEMENTWISE_ABS && input->id <= CBUILTIN_NONTEMPORAL_STORE;
    uint32_t arity = 1;
    if (input->id == CBUILTIN_ELEMENTWISE_FMA || input->id == CBUILTIN_ELEMENTWISE_FSHL || input->id == CBUILTIN_ELEMENTWISE_FSHR)
    {
        arity = 3;
    }
    else if (input->id == CBUILTIN_ELEMENTWISE_ADD_SAT || input->id == CBUILTIN_ELEMENTWISE_SUB_SAT ||
             input->id == CBUILTIN_ELEMENTWISE_MIN || input->id == CBUILTIN_ELEMENTWISE_MAX || input->id == CBUILTIN_NONTEMPORAL_STORE)
    {
        arity = 2;
    }
    const bool count_ok = input->id == CBUILTIN_SHUFFLEVECTOR ? input->argument_count >= 2 :
        input->id == CBUILTIN_ELEMENTWISE_CLZG ? input->argument_count == 1 || input->argument_count == 2 : input->argument_count == arity;
    if (!recognized)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (!count_ok)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if ((input->fp_evaluation_type != 0 && input->fp_evaluation_type != BUSTER_TYPE_DOUBLE && input->fp_evaluation_type != BUSTER_TYPE_LONG_DOUBLE) ||
             (uint32_t)input->lax_vector_conversions > CHECK_BUILTIN_VECTOR_LAX_ALL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else if (input->id <= CBUILTIN_ELEMENTWISE_SUB_SAT)
    {
        check_builtin_vector_math(table, input, output);
    }
    else if (input->id <= CBUILTIN_REDUCE_OR)
    {
        check_builtin_vector_reduce(table, input, output);
    }
    else if (input->id == CBUILTIN_SHUFFLEVECTOR)
    {
        check_builtin_vector_shuffle(table, input, output);
    }
    else if (input->id == CBUILTIN_CONVERTVECTOR)
    {
        check_builtin_vector_convert(table, input, output);
    }
    else if (input->id == CBUILTIN_NONDETERMINISTIC_VALUE)
    {
        const CheckValue value = check_builtin_vector_argument(table, input, output, 0, false);
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            const uint32_t index = type_index(check_builtin_vector_plain(table, value.type));
            if (!check_builtin_vector_is_vector(table, value.type) && (index >= BUSTER_TYPE_BUILTIN_COUNT || check_action_kind(table, value.type) == BUSTER_TYPE_KIND_COMPLEX))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
            }
            else
            {
                output->action.value.type = value.type;
                output->nondeterministic = true;
            }
        }
    }
    else
    {
        check_builtin_vector_nontemporal(table, input, output);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        output->runtime_builtin = input->id;
        output->evaluated_argument_count = output->nondeterministic ? 0 : input->id == CBUILTIN_SHUFFLEVECTOR ? 2 : input->argument_count;
        output->constant_evaluation_unmodeled = input->id <= CBUILTIN_CONVERTVECTOR;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        if (output->element_type == 0)
        {
            const TypeId type = output->memory_type != 0 ? output->memory_type : output->action.value.type;
            output->element_type = check_builtin_vector_element(table, type);
            output->lane_count = check_builtin_vector_lanes(table, type);
        }
        check_builtin_vector_fp_effects(table, input, output);
        output->action.value.flags = output->reads_memory || output->writes_memory || output->nondeterministic ||
            output->reads_fp_environment || output->may_raise_fp_exceptions ? BUSTER_CHECK_SIDE_EFFECT : 0;
        for (uint32_t i = 0; i < output->evaluated_argument_count; i += 1)
        {
            const CheckValue* value = output->converted_arguments + i;
            output->action.value.flags |= value->flags & BUSTER_CHECK_SIDE_EFFECT;
            if (output->action.value.fault == CHECK_FAULT_NONE && value->fault != CHECK_FAULT_NONE)
            {
                output->action.value.fault = value->fault;
                output->action.value.fault_node = value->fault_node;
            }
        }
        for (uint32_t i = 0; i < 3; i += 1)
        {
            output->action.conversions[i] = output->argument_conversions[i];
        }
    }
}

static __attribute__((unused)) void check_builtin_vector(TypeTable* table, const CheckBuiltinVectorAction* inputs, CheckBuiltinVectorResult* outputs, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_vector_one(table, inputs + i, outputs + i);
    }
}

#endif
