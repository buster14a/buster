// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original bounded GNU atomic read/modify/write checking (#148). Include after c_check_builtin_atomic.c; no backend
// module is required. The frozen atomic argument/conversion helpers provide ordinary C parameter conversions and
// source-position-aware enum compatibility only. This module owns operation-specific constraints and never invokes
// a load/store action to stand in for a read/modify/write operation. No source object is read during checking.
//
// source.arguments contains source-checked expressions in API order. The producer retains prior hard diagnostics,
// including diagnostics within legacy ignored tails, before invoking this phase. Only the fixed prefix is read here.
// GNU operations require exact arity. Legacy __sync operations accept trailing protected-variable expressions but
// do not evaluate them at runtime; neither their effects nor their potential evaluation faults propagate here.
// All fixed operands evaluate once, including dynamic GNU weak and memory-order operands. The result is effectful,
// nonconstant typed metadata, never a fabricated result from a source memory access or an already executed store.
//
// order_ice_arguments separately attests actual source integer-constant-expression analysis after ordinary parameter
// conversion. CheckValue's ICE flag alone is not proof. Only GNU order positions may be set; a witnessed value must
// have no effects/fault and exact integer bits unless a conversion-payload gap is explicitly unsupported. Constant
// orders remain metadata even without that witness, but
// only witnessed invalid orders produce Clang's atomic-memory-ordering Warning. Failure order permits 0/1/2/5;
// Clang 23.1.1 deliberately does not reject failure orders stronger than success. Dynamic orders/weak remain valid.
//
// GNU pointer add/sub converts its operand to Linux/x86-64 ptrdiff_t (long) and uses byte deltas without scaling.
// Legacy pointer operations instead convert fixed values to the pointer VALUE type, then operate on raw address bits.
// GNU rejects every _BitInt pointee; legacy accepts only power-of-two bit counts. Sub-byte legacy integers have a
// distinct runtime representation gap. GNU exchange/add/sub also accept floating types except direct x87 long double;
// compare_exchange accepts integer/pointer only. Floating exchange has exact F32/F64 raw-bit helper formats, while
// floating arithmetic remains a valid typed operation with an explicit unsupported runtime reason.
//
// Canonical boolean exchange/CAS/AND/OR/XOR/release preserve valid byte representations. Boolean ADD/SUB/NAND may
// create noncanonical storage bytes; Clang's bool-memory policy and optimization level affect returned values. Such
// operations are source-valid but runtime-unsupported here, never silently masked or normalized by truthiness.
// Generic aggregate variants, unsupported formats, objects wider than 8 bytes and absent/insufficient natural
// alignment proof likewise retain explicit gaps. A consumer must refuse every unsupported bit before calling a
// helper. Format enums describe semantic storage, not catalog capability; source/runtime acceptance is separate.
//
// Original rules audited from llvmorg-23.1.1 SemaChecking.cpp:4792-4826,4924-5560,5589-5961; CGAtomic.cpp:385-481,
// 830-848,1045-1059; CGBuiltin.cpp:299-412. No upstream implementation is copied.
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp
//
// Action/result/fixed-argument slices are naturally aligned, bounded and disjoint from type/output storage, with
// no readable padding. Batches preserve source order while interning pointer/decay types, retain no work, and allow
// null slices for zero count. Legal partitions produce identical semantic results. This phase grants no SOURCE,
// CONSTEXPR, feature or backend capability and never models an unknown-effect helper call as a pure operation.
#ifndef BUSTER_C_CHECK_BUILTIN_ATOMIC_RMW_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_ATOMIC_RMW_INCLUDED

#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_NAND_CHANGED 0x40u
#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST 0x80u
#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW 0x100u
#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BOOL_RMW 0x200u
#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER 0x400u
#define BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER 0x02u

typedef enum CheckBuiltinAtomicRmwOperation
{
    CHECK_BUILTIN_ATOMIC_RMW_NONE,
    CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE,
    CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE,
    CHECK_BUILTIN_ATOMIC_RMW_ADD,
    CHECK_BUILTIN_ATOMIC_RMW_SUB,
    CHECK_BUILTIN_ATOMIC_RMW_AND,
    CHECK_BUILTIN_ATOMIC_RMW_OR,
    CHECK_BUILTIN_ATOMIC_RMW_XOR,
    CHECK_BUILTIN_ATOMIC_RMW_NAND,
    CHECK_BUILTIN_ATOMIC_RMW_RELEASE,
} CheckBuiltinAtomicRmwOperation;

typedef enum CheckBuiltinAtomicRmwReturn
{
    CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN,
    CHECK_BUILTIN_ATOMIC_RMW_VOID,
} CheckBuiltinAtomicRmwReturn;

typedef enum CheckBuiltinAtomicRmwFormat
{
    CHECK_BUILTIN_ATOMIC_RMW_INTEGER_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_BOOL_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_F32_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_F64_BITS,
    CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BITS,
} CheckBuiltinAtomicRmwFormat;

typedef struct CheckBuiltinAtomicRmwAction CheckBuiltinAtomicRmwAction;
struct CheckBuiltinAtomicRmwAction
{
    CheckBuiltinAtomicAction source;
    uint8_t order_ice_arguments;
};

typedef struct CheckBuiltinAtomicRmwResult CheckBuiltinAtomicRmwResult;
struct CheckBuiltinAtomicRmwResult
{
    CheckBuiltinAtomicResult atomic;
    CheckValue converted_arguments[6];
    CheckValue expected_address;
    CheckValue expected_value;
    TypeId operand_type;
    uint32_t fixed_argument_count;
    uint32_t ignored_argument_count;
    CheckBuiltinAtomicRmwOperation operation;
    CheckBuiltinAtomicRmwReturn return_kind;
    CheckBuiltinAtomicRmwFormat format;
    bool legacy;
    bool object_written_on_success;
    bool expected_written_on_failure;
    bool pointer_delta_unscaled;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
};

typedef struct CheckBuiltinAtomicRmwRule CheckBuiltinAtomicRmwRule;
struct CheckBuiltinAtomicRmwRule
{
    CBuiltinId id;
    CheckBuiltinAtomicRmwOperation operation;
    CheckBuiltinAtomicRmwReturn return_kind;
    uint32_t fixed_count;
    bool legacy;
};

static const CheckBuiltinAtomicRmwRule check_builtin_atomic_rmw_rules[] =
{
    { CBUILTIN_ATOMIC_EXCHANGE_N, CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN, 6, false },
    { CBUILTIN_ATOMIC_FETCH_ADD, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_SUB, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_AND, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_OR, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_XOR, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_NAND, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_ADD_FETCH, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_SUB_FETCH, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_OR_FETCH, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_XOR_FETCH, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_NAND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_SYNC_FETCH_AND_ADD, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_SUB, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_AND, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_OR, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_XOR, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_NAND, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_ADD_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_SUB_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_AND_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_OR_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_XOR_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_NAND_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN, 3, true },
    { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, true },
    { CBUILTIN_SYNC_LOCK_TEST_AND_SET, CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_LOCK_RELEASE, CHECK_BUILTIN_ATOMIC_RMW_RELEASE, CHECK_BUILTIN_ATOMIC_RMW_VOID, 1, true },
};

static bool check_builtin_atomic_rmw_floating(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    const bool result = kind == BUSTER_TYPE_KIND_FLOATING ||
        (kind == BUSTER_TYPE_KIND_COMPLEX && check_action_kind(table, type_element(table, type_strip_aligned(table, type))) == BUSTER_TYPE_KIND_FLOATING);
    return result;
}

static void check_builtin_atomic_rmw_object(TypeTable* table, const CheckBuiltinAtomicRmwAction* input, CheckBuiltinAtomicRmwResult* output)
{
    const CheckBuiltinAtomicAction* source = &input->source;
    CheckBuiltinAtomicResult* atomic = &output->atomic;
    const CheckValue* pointer = output->converted_arguments;
    if (check_action_kind(table, pointer->type) != BUSTER_TYPE_KIND_POINTER)
    {
        check_action_diagnose(&atomic->action, CHECK_DIAG_POINTER_REQUIRED, 0);
    }
    else
    {
        const TypeId object = type_element(table, type_strip_aligned(table, pointer->type));
        const TypeId plain = type_strip_aligned(table, object);
        const TypeId unqualified = type_unqualified(plain);
        const uint8_t kind = type_kind(table, plain);
        const bool floating = check_builtin_atomic_rmw_floating(table, plain);
        const bool pointer_value = kind == BUSTER_TYPE_KIND_POINTER;
        const bool arithmetic = output->operation == CHECK_BUILTIN_ATOMIC_RMW_ADD || output->operation == CHECK_BUILTIN_ATOMIC_RMW_SUB;
        const bool exchange = output->operation == CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE;
        const bool compare = output->operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
        const bool integer = check_action_is_integer(table, plain);
        const bool allowed = output->legacy ? integer || pointer_value :
            integer || (pointer_value && (arithmetic || exchange || compare)) ||
            (floating && (arithmetic || exchange) && unqualified != BUSTER_TYPE_LONG_DOUBLE);
        atomic->object_type = object;
        atomic->value_type = output->legacy ? unqualified : plain & ~(BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
        atomic->object_address = *pointer;
        atomic->volatile_access = (type_qualifiers(plain) & BUSTER_TYPE_VOLATILE) != 0;
        if (!check_builtin_atomic_source_complete(table, object, source))
        {
            check_action_diagnose(&atomic->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if (!allowed || (!output->legacy && kind == BUSTER_TYPE_KIND_BIT_INT))
        {
            check_action_diagnose(&atomic->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if ((type_qualifiers(plain) & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&atomic->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
        }
        else
        {
            uint64_t bits = 0;
            uint64_t rank = 0;
            bool is_signed = false;
            if (kind == BUSTER_TYPE_KIND_BIT_INT)
            {
                type_integer_properties(table, unqualified, &rank, &bits, &is_signed);
            }
            atomic->object_size = type_size(table, object);
            if ((kind == BUSTER_TYPE_KIND_BIT_INT && (bits == 0 || (bits & (bits - 1)) != 0)) ||
                (output->legacy && atomic->object_size != 1 && atomic->object_size != 2 && atomic->object_size != 4 && atomic->object_size != 8 && atomic->object_size != 16))
            {
                check_action_diagnose(&atomic->action, CHECK_DIAG_INCOMPATIBLE, 0);
            }
            atomic->required_alignment = atomic->object_size;
            atomic->alignment_known = source->alignment_known;
            atomic->minimum_alignment = source->alignment_known ? source->minimum_alignment : 0;
            if (atomic->object_size != 1 && atomic->object_size != 2 && atomic->object_size != 4 && atomic->object_size != 8)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH;
            }
            if (kind == BUSTER_TYPE_KIND_BIT_INT && bits < 8)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER;
            }
            uint32_t value_bits = 0;
            bool value_signed = false;
            if (integer && unqualified != BUSTER_TYPE_BOOL && check_action_integer_type(table, plain, &value_bits, &value_signed) && value_bits < 8)
            {
                // A completed enum can have a sub-byte integer representation too. Its storage size alone must
                // not silently authorize ordinary byte arithmetic or invent the caller's value normalization.
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER;
            }
            if (!source->alignment_known)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT;
            }
            else if (source->minimum_alignment == 0 || (source->minimum_alignment & (source->minimum_alignment - 1)) != 0)
            {
                check_action_diagnose(&atomic->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else if (source->minimum_alignment < atomic->required_alignment)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT;
                atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT;
                atomic->warning_operands |= 1;
            }
            if (pointer->constant_kind == CHECK_CONSTANT_ADDRESS && pointer->object == 0 && pointer->offset == 0)
            {
                atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL;
                atomic->warning_operands |= 1;
            }
            output->format = unqualified == BUSTER_TYPE_BOOL ? CHECK_BUILTIN_ATOMIC_RMW_BOOL_BITS :
                unqualified == BUSTER_TYPE_FLOAT ? CHECK_BUILTIN_ATOMIC_RMW_F32_BITS :
                unqualified == BUSTER_TYPE_DOUBLE ? CHECK_BUILTIN_ATOMIC_RMW_F64_BITS :
                floating ? CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BITS : CHECK_BUILTIN_ATOMIC_RMW_INTEGER_BITS;
            if (floating && output->format == CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BITS)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT;
            }
            if (floating && arithmetic)
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW;
                output->reads_fp_environment = true;
                output->may_raise_fp_exceptions = true;
            }
            if (unqualified == BUSTER_TYPE_BOOL && (arithmetic || output->operation == CHECK_BUILTIN_ATOMIC_RMW_NAND))
            {
                atomic->unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BOOL_RMW;
            }
            output->pointer_delta_unscaled = pointer_value && arithmetic;
        }
    }
}

static void check_builtin_atomic_rmw_convert(TypeTable* table, const CheckBuiltinAtomicRmwAction* input, CheckBuiltinAtomicRmwResult* output, uint32_t index, TypeId destination)
{
    CheckValue* value = output->converted_arguments + index;
    const TypeId original = type_unqualified(type_strip_aligned(table, value->type));
    const TypeId target = type_unqualified(type_strip_aligned(table, destination));
    const bool wide = (type_kind(table, original) == BUSTER_TYPE_KIND_BIT_INT && table->types.extent[type_index(original)] > 128) ||
        (type_kind(table, target) == BUSTER_TYPE_KIND_BIT_INT && table->types.extent[type_index(target)] > 128);
    if (wide && output->atomic.action.diagnostic == CHECK_DIAG_NONE)
    {
        // A source-valid conversion has no exact wide CheckValue payload or current native consumer. Check ordinary
        // assignability without asking the bounded constant converter to invent low bits or an evaluation fault.
        value->constant_kind = CHECK_CONSTANT_NONE;
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
        output->atomic.unsupported |= BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION;
    }
    check_builtin_atomic_convert(table, &input->source, &output->atomic, value, index, destination);
    if (output->atomic.action.diagnostic == CHECK_DIAG_NONE && original != type_unqualified(type_strip_aligned(table, destination)) &&
        (check_builtin_atomic_rmw_floating(table, original) || check_builtin_atomic_rmw_floating(table, destination)))
    {
        output->reads_fp_environment = true;
        output->may_raise_fp_exceptions = true;
    }
    check_action_inherit(&output->atomic.action.value, value);
}

static void check_builtin_atomic_rmw_order(TypeTable* table, const CheckBuiltinAtomicRmwAction* input, CheckBuiltinAtomicRmwResult* output, uint32_t index, bool failure)
{
    const TypeId original = type_unqualified(type_strip_aligned(table, output->converted_arguments[index].type));
    const uint8_t kind = type_kind(table, original);
    const bool payload_gap = kind == BUSTER_TYPE_KIND_COMPLEX ||
        (kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, original)) ||
        (kind == BUSTER_TYPE_KIND_BIT_INT && table->types.extent[type_index(original)] > 128);
    check_builtin_atomic_rmw_convert(table, input, output, index, BUSTER_TYPE_INT);
    const CheckValue* order = output->converted_arguments + index;
    const bool witnessed = (input->order_ice_arguments & (UINT32_C(1) << index)) != 0;
    const bool known = order->constant_kind == CHECK_CONSTANT_INTEGER && order->fault == CHECK_FAULT_NONE &&
        (order->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    const bool unavailable = payload_gap && (output->atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0 &&
        order->fault == CHECK_FAULT_NONE && (order->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    if (output->atomic.action.diagnostic == CHECK_DIAG_NONE && witnessed && !known && !unavailable)
    {
        check_action_diagnose(&output->atomic.action, CHECK_DIAG_INVALID_TYPE, index);
    }
    if (output->atomic.action.diagnostic == CHECK_DIAG_NONE && known)
    {
        const uint32_t bits = (uint32_t)order->integer;
        if (failure)
        {
            output->atomic.failure_order = bits;
            output->atomic.known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER;
        }
        else
        {
            output->atomic.success_order = bits;
            output->atomic.known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
        }
        const bool valid = failure ? bits == 0 || bits == 1 || bits == 2 || bits == 5 : bits <= 5;
        if (witnessed && !valid)
        {
            output->atomic.warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER;
            output->atomic.warning_operands |= UINT32_C(1) << index;
        }
    }
}

static __attribute__((unused)) void check_builtin_atomic_rmw_one(TypeTable* table, const CheckBuiltinAtomicRmwAction* input, CheckBuiltinAtomicRmwResult* output)
{
    *output = (CheckBuiltinAtomicRmwResult){ 0 };
    const CheckBuiltinAtomicAction* source = &input->source;
    const CheckBuiltinAtomicRmwRule* rule = NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_rules); i += 1)
    {
        if (check_builtin_atomic_rmw_rules[i].id == source->id)
        {
            rule = check_builtin_atomic_rmw_rules + i;
        }
    }
    CheckBuiltinAtomicResult* atomic = &output->atomic;
    if (rule == NULL)
    {
        atomic->unsupported = BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID;
        check_action_diagnose(&atomic->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (source->argument_count < rule->fixed_count || (!rule->legacy && source->argument_count != rule->fixed_count))
    {
        check_action_diagnose(&atomic->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (source->arguments == NULL)
    {
        check_action_diagnose(&atomic->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        const bool compare = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
        const uint32_t allowed_ice = rule->legacy ? 0 : compare ? (UINT32_C(1) << 4) | (UINT32_C(1) << 5) : UINT32_C(1) << 2;
        if ((input->order_ice_arguments & ~allowed_ice) != 0)
        {
            check_action_diagnose(&atomic->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        output->operation = rule->operation;
        output->return_kind = rule->return_kind;
        output->legacy = rule->legacy;
        output->fixed_argument_count = rule->fixed_count;
        output->ignored_argument_count = source->argument_count - rule->fixed_count;
        output->object_written_on_success = compare;
        output->expected_written_on_failure = compare && !rule->legacy;
        atomic->action.value.type = BUSTER_TYPE_VOID;
        atomic->action.value.category = CHECK_RVALUE;
        atomic->action.value.flags = BUSTER_CHECK_SIDE_EFFECT;
        atomic->evaluated_operands = (UINT32_C(1) << rule->fixed_count) - 1;
        atomic->runtime_builtin = source->id;
        atomic->compiler_barrier = true;
        atomic->reads_memory = rule->operation != CHECK_BUILTIN_ATOMIC_RMW_RELEASE;
        atomic->writes_memory = true;
        atomic->rmw = rule->operation != CHECK_BUILTIN_ATOMIC_RMW_RELEASE;
        for (uint32_t i = 0; i < rule->fixed_count && atomic->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            output->converted_arguments[i] = check_builtin_atomic_argument(table, source, atomic, i);
            check_action_inherit(&atomic->action.value, output->converted_arguments + i);
        }
        if (atomic->action.diagnostic == CHECK_DIAG_NONE)
        {
            check_builtin_atomic_rmw_object(table, input, output);
        }
        if (atomic->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->operand_type = atomic->value_type;
            if (compare)
            {
                const TypeId expected_type = rule->legacy ? atomic->value_type : type_pointer(table, type_unqualified(atomic->value_type));
                if (expected_type == 0)
                {
                    check_action_diagnose(&atomic->action, CHECK_DIAG_RESOURCE, 1);
                }
                else
                {
                    check_builtin_atomic_rmw_convert(table, input, output, 1, expected_type);
                    if (rule->legacy)
                    {
                        output->expected_value = output->converted_arguments[1];
                    }
                    else
                    {
                        output->expected_address = output->converted_arguments[1];
                        if (output->expected_address.constant_kind == CHECK_CONSTANT_ADDRESS && output->expected_address.object == 0 && output->expected_address.offset == 0)
                        {
                            atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL;
                            atomic->warning_operands |= UINT32_C(1) << 1;
                        }
                    }
                    if (atomic->action.diagnostic == CHECK_DIAG_NONE)
                    {
                        check_builtin_atomic_rmw_convert(table, input, output, 2, atomic->value_type);
                        atomic->store_value = output->converted_arguments[2];
                    }
                }
                if (!rule->legacy && atomic->action.diagnostic == CHECK_DIAG_NONE)
                {
                    check_builtin_atomic_rmw_convert(table, input, output, 3, BUSTER_TYPE_BOOL);
                    check_builtin_atomic_rmw_order(table, input, output, 4, false);
                    check_builtin_atomic_rmw_order(table, input, output, 5, true);
                }
            }
            else if (rule->operation != CHECK_BUILTIN_ATOMIC_RMW_RELEASE)
            {
                if (!rule->legacy && output->pointer_delta_unscaled)
                {
                    output->operand_type = BUSTER_TYPE_LONG;
                }
                check_builtin_atomic_rmw_convert(table, input, output, 1, output->operand_type);
                atomic->store_value = output->converted_arguments[1];
                if (!rule->legacy && atomic->action.diagnostic == CHECK_DIAG_NONE)
                {
                    check_builtin_atomic_rmw_order(table, input, output, 2, false);
                }
            }
            if (rule->legacy)
            {
                atomic->success_order = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE ? 3 : 5;
                atomic->failure_order = compare ? 5 : 0;
                atomic->known_orders = BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER |
                    (compare ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER : 0);
                atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST;
                if (rule->operation == CHECK_BUILTIN_ATOMIC_RMW_NAND)
                {
                    atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_NAND_CHANGED;
                }
            }
            atomic->action.value.type = rule->return_kind == CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN ? BUSTER_TYPE_BOOL :
                rule->return_kind == CHECK_BUILTIN_ATOMIC_RMW_VOID ? BUSTER_TYPE_VOID : atomic->value_type;
        }
    }
    atomic->action.value.constant_kind = CHECK_CONSTANT_NONE;
    atomic->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
}

static __attribute__((unused)) void check_builtin_atomic_rmw(TypeTable* table, const CheckBuiltinAtomicRmwAction* actions, CheckBuiltinAtomicRmwResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_atomic_rmw_one(table, actions + i, results + i);
    }
}

#endif
