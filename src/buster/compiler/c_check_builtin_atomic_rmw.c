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
// compare_exchange accepts integer/pointer only. Clang's min/max extensions (fetch_min, max_fetch, ...) accept integers
// and the same floating types, never pointers; its IEEE maximum and minimum (fetch_fmaximum, ...) those floating types
// alone. Floating exchange and the IEEE maximum and minimum have exact F32/F64 helper formats, while floating
// arithmetic and min/max remain valid typed operations with an explicit unsupported runtime reason.
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
    // clang's GNU extensions: the smaller or the larger of the value and the operand, by the value type's ordering.
    CHECK_BUILTIN_ATOMIC_RMW_MIN,
    CHECK_BUILTIN_ATOMIC_RMW_MAX,
    // clang's wrapping increment and decrement (atomicrmw uinc_wrap and udec_wrap), unsigned in the value's width: 0 for a
    // value at or above the operand and the value plus one otherwise; the operand for a value of 0 or above it and the
    // value less one otherwise.
    CHECK_BUILTIN_ATOMIC_RMW_UINC,
    CHECK_BUILTIN_ATOMIC_RMW_UDEC,
    // GNU's byte flag: 1 exchanged into the first byte of any object, whether it was set the result, and 0 stored there.
    CHECK_BUILTIN_ATOMIC_RMW_TEST_AND_SET,
    CHECK_BUILTIN_ATOMIC_RMW_CLEAR,
    // clang's IEEE 754-2019 maximum and minimum of a floating value and the operand (atomicrmw fmaximum, fminimum,
    // fmaximumnum and fminimumnum): -0 below +0, and a NaN the result where either is one, or in the _num forms the other
    // operand where only one is. Their NaN results are LLVM's x86 expansion's (c_builtin_atomic_rmw_helpers.c).
    CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM,
    CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM_NUM,
    CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM,
    CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM_NUM,
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
    // A legacy builtin of an ordinary prototype's, clang's __sync_fetch_and_min and the other three (int or unsigned int
    // (volatile T *, T)): its value type T, whose pointer the first argument converts to as a call's argument does, and
    // a call takes exactly its arguments. 0 for a builtin whose types its object gives.
    TypeId prototype;
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
    { CBUILTIN_ATOMIC_FETCH_MIN, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_MAX, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_MIN_FETCH, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_MAX_FETCH, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    // clang's __sync_swap is __sync_lock_test_and_set's exchange, both sequentially consistent (CGBuiltin.cpp's
    // EmitBinaryAtomic of Xchg).
    { CBUILTIN_SYNC_SWAP, CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    // clang's own legacy minimum and maximum, of int signed and of unsigned int unsigned (atomicrmw min, max, umin and umax).
    { CBUILTIN_SYNC_FETCH_AND_MIN, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true, BUSTER_TYPE_INT },
    { CBUILTIN_SYNC_FETCH_AND_MAX, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true, BUSTER_TYPE_INT },
    { CBUILTIN_SYNC_FETCH_AND_UMIN, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true, BUSTER_TYPE_UINT },
    { CBUILTIN_SYNC_FETCH_AND_UMAX, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true, BUSTER_TYPE_UINT },
    { CBUILTIN_ATOMIC_FETCH_UINC, CHECK_BUILTIN_ATOMIC_RMW_UINC, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_UDEC, CHECK_BUILTIN_ATOMIC_RMW_UDEC, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_TEST_AND_SET, CHECK_BUILTIN_ATOMIC_RMW_TEST_AND_SET, CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN, 2, false },
    { CBUILTIN_ATOMIC_CLEAR, CHECK_BUILTIN_ATOMIC_RMW_CLEAR, CHECK_BUILTIN_ATOMIC_RMW_VOID, 2, false },
    { CBUILTIN_ATOMIC_FETCH_FMAXIMUM, CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_FMAXIMUM_NUM, CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM_NUM, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_FMINIMUM, CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM, CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM_NUM, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
};

// Whether a rule's operation is clang's IEEE maximum or minimum (__atomic_fetch_fmaximum and the other three).
static bool check_builtin_atomic_rmw_ieee(CheckBuiltinAtomicRmwOperation operation)
{
    return operation >= CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM && operation <= CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM_NUM;
}

// Whether a rule's operation is on GNU's byte flag, whose order is its second argument (__atomic_test_and_set and
// __atomic_clear).
static bool check_builtin_atomic_rmw_byte(CheckBuiltinAtomicRmwOperation operation)
{
    return operation == CHECK_BUILTIN_ATOMIC_RMW_TEST_AND_SET || operation == CHECK_BUILTIN_ATOMIC_RMW_CLEAR;
}

// The read/modify/write rule of a builtin identity, or NULL: the source checker's dispatch to this module, which every
// builtin call's op takes, so that an identity outside the atomic families' range reads no rule.
static __attribute__((unused)) const CheckBuiltinAtomicRmwRule* check_builtin_atomic_rmw_rule(CBuiltinId id)
{
    const CheckBuiltinAtomicRmwRule* rule = NULL;
    const bool family = id >= CBUILTIN_ATOMIC_EXCHANGE_N && id <= CBUILTIN_SYNC_FETCH_AND_UMAX;
    for (uint32_t i = 0; family && i < BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_rules); i += 1)
    {
        if (check_builtin_atomic_rmw_rules[i].id == id)
        {
            rule = check_builtin_atomic_rmw_rules + i;
        }
    }
    return rule;
}

// The order positions whose source arguments are integer constant expressions, as the source checker proves them (a
// value's ICE flag from the expression's own analysis): the action's order_ice_arguments.
static __attribute__((unused)) uint8_t check_builtin_atomic_rmw_order_ice(CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(id);
    uint8_t ice = 0;
    if (rule != NULL && !rule->legacy && arguments != NULL && count == rule->fixed_count)
    {
        const bool compare = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
        for (uint32_t i = compare ? 4 : check_builtin_atomic_rmw_byte(rule->operation) ? 1 : 2; i < count; i += 1)
        {
            ice |= (arguments[i].flags & BUSTER_CHECK_ICE) != 0 ? (uint8_t)(1u << i) : 0;
        }
    }
    return ice;
}

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
    else if (check_builtin_atomic_rmw_byte(output->operation))
    {
        // GNU's byte flag takes a pointer to any object but a const one, void and incomplete types too, and accesses its
        // first byte: a char with the pointee's volatile, always aligned (SemaChecking's TestAndSetByte and ClearByte).
        const uint32_t qualifiers = type_qualifiers(type_strip_aligned(table, type_element(table, type_strip_aligned(table, pointer->type))));
        atomic->object_type = type_qualify(table, BUSTER_TYPE_CHAR, qualifiers & BUSTER_TYPE_VOLATILE);
        atomic->value_type = BUSTER_TYPE_CHAR;
        atomic->object_address = *pointer;
        atomic->volatile_access = (qualifiers & BUSTER_TYPE_VOLATILE) != 0;
        if ((qualifiers & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&atomic->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
        }
        else
        {
            atomic->object_size = 1;
            atomic->required_alignment = 1;
            atomic->alignment_known = true;
            atomic->minimum_alignment = source->alignment_known && source->minimum_alignment != 0 ? source->minimum_alignment : 1;
            if (pointer->constant_kind == CHECK_CONSTANT_ADDRESS && pointer->object == 0 && pointer->offset == 0)
            {
                atomic->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL;
                atomic->warning_operands |= 1;
            }
            output->format = CHECK_BUILTIN_ATOMIC_RMW_INTEGER_BITS;
        }
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
        const bool ordered = output->operation == CHECK_BUILTIN_ATOMIC_RMW_MIN || output->operation == CHECK_BUILTIN_ATOMIC_RMW_MAX;
        const bool wrapping = output->operation == CHECK_BUILTIN_ATOMIC_RMW_UINC || output->operation == CHECK_BUILTIN_ATOMIC_RMW_UDEC;
        const bool ieee = check_builtin_atomic_rmw_ieee(output->operation);
        const bool integer = check_action_is_integer(table, plain);
        // Clang's min and max take integers and its floating types but not pointers (AOEVT_FP alone), its wrapping
        // increment and decrement integers alone (bool among them), and its IEEE maximum and minimum its floating types
        // alone, complex ones among them, but long double ("must be a pointer to floating point type").
        const bool allowed = output->legacy ? integer || pointer_value :
            wrapping ? integer :
            ieee ? floating && unqualified != BUSTER_TYPE_LONG_DOUBLE :
            integer || (pointer_value && (arithmetic || exchange || compare)) ||
            (floating && (arithmetic || exchange || ordered) && unqualified != BUSTER_TYPE_LONG_DOUBLE);
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
            if (floating && (arithmetic || ordered || ieee))
            {
                // The IEEE maximum and minimum of a float or a double have leaves (c_builtin_atomic_rmw_helpers.c); floating
                // addition, subtraction, min and max none yet.
                atomic->unsupported |= ieee ? 0 : BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW;
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
        // A clear's order is a store's (isValidOrderingForOp): not consume, acquire or acq_rel.
        const bool store = output->operation == CHECK_BUILTIN_ATOMIC_RMW_CLEAR;
        const bool valid = failure ? bits == 0 || bits == 1 || bits == 2 || bits == 5 :
                           store   ? bits == 0 || bits == 3 || bits == 5 :
                                     bits <= 5;
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
    const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(source->id);
    CheckBuiltinAtomicResult* atomic = &output->atomic;
    if (rule == NULL)
    {
        atomic->unsupported = BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID;
        check_action_diagnose(&atomic->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (source->argument_count < rule->fixed_count || ((!rule->legacy || rule->prototype != 0) && source->argument_count != rule->fixed_count))
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
        const bool byte = check_builtin_atomic_rmw_byte(rule->operation);
        // A store's result is no read/modify/write of its object: a legacy release's and a clear's.
        const bool store = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE || rule->operation == CHECK_BUILTIN_ATOMIC_RMW_CLEAR;
        const uint32_t allowed_ice = rule->legacy ? 0 : compare ? (UINT32_C(1) << 4) | (UINT32_C(1) << 5) : byte ? UINT32_C(1) << 1 : UINT32_C(1) << 2;
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
        atomic->reads_memory = !store;
        atomic->writes_memory = true;
        atomic->rmw = !store;
        for (uint32_t i = 0; i < rule->fixed_count && atomic->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            output->converted_arguments[i] = check_builtin_atomic_argument(table, source, atomic, i);
            check_action_inherit(&atomic->action.value, output->converted_arguments + i);
        }
        CheckBuiltinAtomicRmwAction prototyped = *input;
        if (rule->prototype != 0 && atomic->action.diagnostic == CHECK_DIAG_NONE)
        {
            // The argument converts to the prototype's volatile T * as a call's does (a pointer to T of the other
            // signedness with clang's pointer-sign warning, an incompatible one an error). Its object's alignment is the
            // argument's, but from a void * the parameter's pointee type's, as clang's code generation gives it
            // (EmitPointerWithAlignment looks through no conversion from void *).
            const TypeId pointer = output->converted_arguments[0].type;
            const bool from_void = check_action_kind(table, pointer) == BUSTER_TYPE_KIND_POINTER &&
                                   check_action_kind(table, type_element(table, type_strip_aligned(table, pointer))) == BUSTER_TYPE_KIND_VOID;
            const TypeId parameter = type_pointer(table, type_qualify(table, rule->prototype, BUSTER_TYPE_VOLATILE));
            if (parameter == 0)
            {
                check_action_diagnose(&atomic->action, CHECK_DIAG_RESOURCE, 0);
            }
            else
            {
                check_builtin_atomic_rmw_convert(table, input, output, 0, parameter);
            }
            if (from_void)
            {
                prototyped.source.alignment_known = true;
                prototyped.source.minimum_alignment = type_alignment(table, rule->prototype);
            }
        }
        if (atomic->action.diagnostic == CHECK_DIAG_NONE)
        {
            check_builtin_atomic_rmw_object(table, &prototyped, output);
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
            else if (byte)
            {
                check_builtin_atomic_rmw_order(table, input, output, 1, false);
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
                // clang's implicit-order warning is its overloaded builtins' (BuiltinAtomicOverloaded), no prototype's.
                atomic->warnings |= rule->prototype == 0 ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST : 0;
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
