// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original typed GNU atomic actions (#148). Include after c_builtin_catalog.c and c_check_actions.c.
// This bounded slice checks load_n, store_n, synchronize and thread/signal fences. Other identities are explicitly
// unsupported. The producer owns argument graph identities, evaluation order and address provenance. Every listed
// argument is evaluated exactly once, including dynamic memory orders; no action reads the source program's memory.
//
// alignment_known supplies a positive power-of-two lower bound proved for the actual addressed object, including
// packed members and reduced alignment. A pointer type alone is not supplied address-provenance evidence. Unknown
// or insufficient alignment leaves valid source semantics intact and marks the native-helper path unsupported.
// The helper path requires naturally aligned 1/2/4/8-byte objects, preserves volatile accesses and must use true
// atomic helpers through unknown-effect calls (or atomic typed operations), never optimizer-visible ordinary loads.
//
// Actions/results and each arguments slice are ordinarily aligned, bounded and disjoint from output/type storage;
// no readable padding is required. Empty batches allow null slices. Batches preserve input order while interning
// decay types. There is no retained work, and legal batch partitions produce identical semantic results. These
// actions establish no catalog SOURCE/CONSTEXPR capability or runtime-consumer acceptance by themselves.
#ifndef BUSTER_C_CHECK_BUILTIN_ATOMIC_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_ATOMIC_INCLUDED

#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER 0x01u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT 0x02u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL 0x04u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS 0x08u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN 0x10u
#define BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER 0x20u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID 0x01u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH 0x02u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT 0x04u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT 0x08u
#define BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION 0x10u
#define BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER 0x01u

typedef enum CheckBuiltinAtomicFenceScope
{
    CHECK_BUILTIN_ATOMIC_FENCE_NONE,
    CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM,
    CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL,
} CheckBuiltinAtomicFenceScope;

typedef struct CheckBuiltinAtomicAction CheckBuiltinAtomicAction;
struct CheckBuiltinAtomicAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    uint64_t minimum_alignment;
    bool alignment_known;
};

typedef struct CheckBuiltinAtomicResult CheckBuiltinAtomicResult;
struct CheckBuiltinAtomicResult
{
    CheckActionResult action;
    CheckConversion argument_conversions[6];
    // Converted pointer/value metadata, never an invented load result or an already executed store.
    CheckValue object_address;
    CheckValue store_value;
    TypeId object_type;
    TypeId value_type;
    uint64_t object_size;
    uint64_t required_alignment;
    uint64_t minimum_alignment;
    uint32_t evaluated_operands;
    uint32_t success_order;
    uint32_t failure_order;
    uint32_t known_orders;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    CheckBuiltinAtomicFenceScope fence_scope;
    bool alignment_known;
    bool volatile_access;
    bool reads_memory;
    bool writes_memory;
    bool rmw;
    // A conservative compiler ordering boundary is permitted for every supported request. Consumers may
    // strengthen valid orderings to sequential consistency while retaining all argument evaluations.
    bool compiler_barrier;
};

static bool check_builtin_atomic_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinAtomicAction* input)
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

static CheckValue check_builtin_atomic_argument(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, uint32_t index)
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
        if ((value.category == CHECK_LVALUE || kind == BUSTER_TYPE_KIND_ENUM) && !type_kind_is_array(kind) &&
            !check_builtin_atomic_source_complete(table, base, input))
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

static void check_builtin_atomic_convert(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, CheckValue* value, uint32_t index, TypeId destination)
{
    bool accepted = output->action.diagnostic == CHECK_DIAG_NONE;
    const uint8_t source_kind = check_action_kind(table, value->type);
    if (accepted && (source_kind == BUSTER_TYPE_KIND_COMPLEX ||
                     (source_kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, value->type))))
    {
        output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION;
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, index);
        accepted = false;
    }
    else if (accepted && check_action_kind(table, destination) == BUSTER_TYPE_KIND_POINTER && source_kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId to = type_element(table, type_strip_aligned(table, destination));
        const TypeId from = type_element(table, type_strip_aligned(table, value->type));
        const TypeId to_base = type_unqualified(type_strip_aligned(table, to));
        const TypeId from_base = type_unqualified(type_strip_aligned(table, from));
        const uint8_t to_kind = check_action_kind(table, to);
        const uint8_t from_kind = check_action_kind(table, from);
        const bool void_pointer = to_kind == BUSTER_TYPE_KIND_VOID || from_kind == BUSTER_TYPE_KIND_VOID;
        const bool function_pointer = to_kind == BUSTER_TYPE_KIND_FUNCTION || from_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool char_pair = (to_base == BUSTER_TYPE_CHAR || to_base == BUSTER_TYPE_SCHAR || to_base == BUSTER_TYPE_UCHAR) &&
                               (from_base == BUSTER_TYPE_CHAR || from_base == BUSTER_TYPE_SCHAR || from_base == BUSTER_TYPE_UCHAR);
        accepted = void_pointer || char_pair || check_action_compatible(table, to_base, from_base, &output->action);
        if (accepted)
        {
            output->warnings |= (type_qualifiers(from) & ~type_qualifiers(to)) != 0 ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS : 0;
            output->warnings |= void_pointer && function_pointer ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER : 0;
            output->warnings |= !void_pointer && char_pair && to_base != from_base ? BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN : 0;
            if ((type_qualifiers(from) & ~type_qualifiers(to)) != 0 || (void_pointer && function_pointer) || (!void_pointer && char_pair && to_base != from_base))
            {
                output->warning_operands |= UINT32_C(1) << index;
            }
        }
    }
    else if (accepted)
    {
        const CheckAction context = {
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        accepted = check_action_assignable(table, destination, value, &context, &output->action);
    }
    if (accepted)
    {
        check_action_conversion(table, value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, output->argument_conversions + index, input->node);
    }
    else if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
    }
}

static void check_builtin_atomic_object(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, const CheckValue* pointer)
{
    if (check_action_kind(table, pointer->type) != BUSTER_TYPE_KIND_POINTER)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, 0);
    }
    else
    {
        const TypeId object = type_element(table, type_strip_aligned(table, pointer->type));
        const TypeId plain = type_strip_aligned(table, object);
        const uint8_t kind = type_kind(table, plain);
        output->object_type = object;
        // Clang removes the inferred object's local const/volatile, preserving pointer restrict qualification.
        output->value_type = plain & ~(BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
        output->object_address = *pointer;
        output->volatile_access = (type_qualifiers(plain) & BUSTER_TYPE_VOLATILE) != 0;
        if (!check_builtin_atomic_source_complete(table, object, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if ((!check_action_is_integer(table, plain) && kind != BUSTER_TYPE_KIND_POINTER) || kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            // GNU _n builtins use ordinary integer/pointer objects, not _Atomic(T), floating or record objects.
            // Clang additionally prohibits _BitInt even when its representation would fit a native helper.
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (input->id == CBUILTIN_ATOMIC_STORE_N && (type_qualifiers(plain) & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
        }
        else
        {
            output->object_size = type_size(table, object);
            output->required_alignment = output->object_size;
            output->alignment_known = input->alignment_known;
            output->minimum_alignment = input->alignment_known ? input->minimum_alignment : 0;
            if (output->object_size != 1 && output->object_size != 2 && output->object_size != 4 && output->object_size != 8)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH;
            }
            if (!input->alignment_known)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT;
            }
            else if (input->minimum_alignment == 0 || (input->minimum_alignment & (input->minimum_alignment - 1)) != 0)
            {
                // Invalid producer metadata is distinct from valid-but-under-aligned source input.
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else if (input->minimum_alignment < output->required_alignment)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT;
                output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT;
                output->warning_operands |= 1;
            }
            if (pointer->constant_kind == CHECK_CONSTANT_ADDRESS && pointer->object == 0 && pointer->offset == 0)
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL;
                output->warning_operands |= 1;
            }
        }
    }
}

static void check_builtin_atomic_order(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output, CheckValue* order, uint32_t index)
{
    check_builtin_atomic_convert(table, input, output, order, index, BUSTER_TYPE_INT);
    if (output->action.diagnostic == CHECK_DIAG_NONE && order->constant_kind == CHECK_CONSTANT_INTEGER && order->fault == CHECK_FAULT_NONE &&
        (order->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
    {
        output->success_order = (uint32_t)order->integer;
        output->known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
        const bool load = input->id == CBUILTIN_ATOMIC_LOAD_N;
        const bool store = input->id == CBUILTIN_ATOMIC_STORE_N;
        const uint32_t value = output->success_order;
        const bool valid = value <= 5 && (!load || (value != 3 && value != 4)) && (!store || (value != 1 && value != 2 && value != 4));
        // Clang diagnoses invalid load/store orders only when getIntegerConstantExpr succeeds. Fences have
        // ordinary void(int) prototypes and no corresponding Sema warning; their raw order remains metadata.
        if ((load || store) && (order->flags & BUSTER_CHECK_ICE) != 0 && !valid)
        {
            output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER;
            output->warning_operands |= UINT32_C(1) << index;
        }
    }
}

static __attribute__((unused)) void check_builtin_atomic_one(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    *output = (CheckBuiltinAtomicResult){ 0 };
    const bool load = input->id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = input->id == CBUILTIN_ATOMIC_STORE_N;
    const bool synchronize = input->id == CBUILTIN_SYNC_SYNCHRONIZE;
    const bool fence = input->id == CBUILTIN_ATOMIC_THREAD_FENCE || input->id == CBUILTIN_ATOMIC_SIGNAL_FENCE;
    const uint32_t arity = load ? 2 : store ? 3 : fence ? 1 : 0;
    if (!load && !store && !synchronize && !fence)
    {
        output->unsupported = BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID;
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != arity)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (arity != 0 && input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        CheckValue values[3] = { 0 };
        output->action.value.type = BUSTER_TYPE_VOID;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags = BUSTER_CHECK_SIDE_EFFECT;
        output->evaluated_operands = (UINT32_C(1) << arity) - 1;
        output->runtime_builtin = input->id;
        output->compiler_barrier = true;
        output->reads_memory = load;
        output->writes_memory = store;
        for (uint32_t i = 0; i < arity && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_atomic_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && (load || store))
        {
            check_builtin_atomic_object(table, input, output, values);
            if (output->action.diagnostic == CHECK_DIAG_NONE && store)
            {
                check_builtin_atomic_convert(table, input, output, values + 1, 1, output->value_type);
                output->store_value = values[1];
                check_action_inherit(&output->action.value, values + 1);
            }
            if (output->action.diagnostic == CHECK_DIAG_NONE)
            {
                const uint32_t order = store ? 2 : 1;
                check_builtin_atomic_order(table, input, output, values + order, order);
                check_action_inherit(&output->action.value, values + order);
                output->action.value.type = load ? output->value_type : BUSTER_TYPE_VOID;
            }
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && fence)
        {
            check_builtin_atomic_order(table, input, output, values, 0);
            check_action_inherit(&output->action.value, values);
            output->fence_scope = input->id == CBUILTIN_ATOMIC_SIGNAL_FENCE ? CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL : CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM;
        }
        else if (synchronize)
        {
            output->success_order = 5;
            output->known_orders = BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
            output->fence_scope = CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM;
        }
    }
    // An atomic access is never a source constant expression, even if its address/store bits are known.
    output->action.value.constant_kind = CHECK_CONSTANT_NONE;
    output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
}

static __attribute__((unused)) void check_builtin_atomic(TypeTable* table, const CheckBuiltinAtomicAction* actions, CheckBuiltinAtomicResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_atomic_one(table, actions + i, results + i);
    }
}

#endif
