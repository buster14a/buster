// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C11 atomic source constraints and the common atomic-effect handoff. Unlike GNU's _n forms, the object is an
// _Atomic(T), and load/store/exchange/compare-exchange also take records and extended scalar representations.
// Argument identities remain in source order; the lowering supplies init's order and compare-exchange's weak flag.
// GNU's generic forms (__atomic_load, __atomic_store, __atomic_exchange and __atomic_compare_exchange), whose values are
// pointers to them, take the same representations, which their lowering copies through the same buffers. The scoped
// families (clang's __scoped_atomic_*, __hip_atomic_* and __opencl_atomic_*) are their base builtins' operations with a
// scope (check_builtin_atomic_family).
// Rules audited against llvmorg-23.1.1 SemaChecking.cpp:4990-5558; no upstream implementation is copied.
#ifndef BUSTER_C_CHECK_BUILTIN_ATOMIC_C11_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_ATOMIC_C11_INCLUDED

static bool check_builtin_atomic_c11(CBuiltinId id)
{
    return id >= CBUILTIN_C11_ATOMIC_INIT && id <= CBUILTIN_C11_ATOMIC_IS_LOCK_FREE;
}

static CBuiltinId check_builtin_atomic_c11_runtime(CBuiltinId id)
{
    static const CBuiltinId mapped[] = {
        CBUILTIN_ATOMIC_STORE_N,
        CBUILTIN_ATOMIC_LOAD_N,
        CBUILTIN_ATOMIC_STORE_N,
        CBUILTIN_ATOMIC_EXCHANGE_N,
        CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N,
        CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N,
        CBUILTIN_ATOMIC_FETCH_ADD,
        CBUILTIN_ATOMIC_FETCH_SUB,
        CBUILTIN_ATOMIC_FETCH_AND,
        CBUILTIN_ATOMIC_FETCH_OR,
        CBUILTIN_ATOMIC_FETCH_XOR,
        CBUILTIN_ATOMIC_FETCH_NAND,
        CBUILTIN_ATOMIC_FETCH_MIN,
        CBUILTIN_ATOMIC_FETCH_MAX,
        CBUILTIN_ATOMIC_THREAD_FENCE,
        CBUILTIN_ATOMIC_SIGNAL_FENCE,
        CBUILTIN_ATOMIC_IS_LOCK_FREE,
    };
    return check_builtin_atomic_c11(id) ? mapped[id - CBUILTIN_C11_ATOMIC_INIT] : id;
}

static bool check_builtin_atomic_c11_native(const TypeTable* table, CBuiltinId id, TypeId value, uint64_t bytes, bool aligned)
{
    const uint8_t kind = value != 0 ? check_action_kind(table, value) : BUSTER_TYPE_KIND_VOID;
    const bool fence = id == CBUILTIN_C11_ATOMIC_THREAD_FENCE || id == CBUILTIN_C11_ATOMIC_SIGNAL_FENCE;
    const bool arithmetic = id >= CBUILTIN_C11_ATOMIC_FETCH_ADD && id <= CBUILTIN_C11_ATOMIC_FETCH_MAX;
    const bool scalar = check_action_is_integer(table, value) || kind == BUSTER_TYPE_KIND_POINTER || value == BUSTER_TYPE_FLOAT ||
                        value == BUSTER_TYPE_DOUBLE || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ||
                        kind == BUSTER_TYPE_KIND_COMPLEX;
    const bool result =
        fence || (aligned && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) && scalar &&
                  (!arithmetic || (value != BUSTER_TYPE_FLOAT && value != BUSTER_TYPE_DOUBLE && value != BUSTER_TYPE_BOOL)));
    return result;
}

static void check_builtin_atomic_c11_one(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    *output = (CheckBuiltinAtomicResult){0};
    const CBuiltinId id = input->id;
    const bool init = id == CBUILTIN_C11_ATOMIC_INIT;
    const bool load = id == CBUILTIN_C11_ATOMIC_LOAD;
    const bool store = init || id == CBUILTIN_C11_ATOMIC_STORE;
    const bool compare = id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG || id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK;
    const bool fence = id == CBUILTIN_C11_ATOMIC_THREAD_FENCE || id == CBUILTIN_C11_ATOMIC_SIGNAL_FENCE;
    const bool lock_free = id == CBUILTIN_C11_ATOMIC_IS_LOCK_FREE;
    const bool arithmetic = id >= CBUILTIN_C11_ATOMIC_FETCH_ADD && id <= CBUILTIN_C11_ATOMIC_FETCH_MAX;
    const bool add_sub = id == CBUILTIN_C11_ATOMIC_FETCH_ADD || id == CBUILTIN_C11_ATOMIC_FETCH_SUB;
    const bool min_max = id == CBUILTIN_C11_ATOMIC_FETCH_MIN || id == CBUILTIN_C11_ATOMIC_FETCH_MAX;
    const uint32_t count = fence || lock_free ? 1 : init || load ? 2 : compare ? 5 : 3;
    CheckValue values[6] = {0};
    if (input->argument_count != count)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        output->runtime_builtin = check_builtin_atomic_c11_runtime(id);
        output->action.value = (CheckValue){.type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = lock_free ? 0 : BUSTER_CHECK_SIDE_EFFECT};
        output->evaluated_operands = (1u << count) - 1;
        output->compiler_barrier = !lock_free;
        for (uint32_t i = 0; i < count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_atomic_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && (fence || lock_free))
        {
            check_builtin_atomic_convert(table, input, output, values, 0, lock_free ? BUSTER_TYPE_ULONG : BUSTER_TYPE_INT);
            output->action.value.type = lock_free ? BUSTER_TYPE_BOOL : BUSTER_TYPE_VOID;
            output->fence_scope = id == CBUILTIN_C11_ATOMIC_SIGNAL_FENCE ? CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL
                                  : fence                                ? CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM
                                                                         : CHECK_BUILTIN_ATOMIC_FENCE_NONE;
            if (values[0].constant_kind == CHECK_CONSTANT_INTEGER && !lock_free)
            {
                output->success_order = (uint32_t)values[0].integer;
                output->known_orders = BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
            }
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            const bool pointer = check_action_kind(table, values[0].type) == BUSTER_TYPE_KIND_POINTER;
            const TypeId object = pointer ? type_element(table, type_strip_aligned(table, values[0].type)) : 0;
            const TypeId atomic = pointer ? type_strip_aligned(table, object) : 0;
            // HIP's object is a plain one, of any type of a size but an _Atomic one (clang's "trivially-copyable").
            const bool wrapped = pointer && type_kind(table, atomic) == BUSTER_TYPE_KIND_ATOMIC;
            if (!pointer || wrapped == input->plain_object ||
                (input->plain_object && check_builtin_atomic_source_complete(table, atomic, input) &&
                 (check_action_kind(table, atomic) == BUSTER_TYPE_KIND_FUNCTION || type_size(table, atomic) == 0)))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
            }
            else if (!load && (type_qualifiers(atomic) & BUSTER_TYPE_CONST) != 0)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
            }
            else if (!check_builtin_atomic_source_complete(table, atomic, input))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
            }
            else
            {
                const TypeId value = type_conversion_operand(table, atomic);
                const uint8_t kind = check_action_kind(table, value);
                const bool integer = check_action_is_integer(table, value);
                const bool allowed = integer || (add_sub && kind == BUSTER_TYPE_KIND_POINTER) ||
                                     ((add_sub || min_max) && kind == BUSTER_TYPE_KIND_FLOATING && value != BUSTER_TYPE_LONG_DOUBLE);
                output->object_address = values[0];
                output->object_type = object;
                output->value_type = value;
                output->object_size = type_size(table, object);
                output->required_alignment = output->object_size;
                output->minimum_alignment = input->minimum_alignment;
                output->alignment_known = input->alignment_known;
                output->volatile_access = (type_qualifiers(atomic) & BUSTER_TYPE_VOLATILE) != 0;
                output->reads_memory = !store;
                output->writes_memory = !load;
                output->rmw = !store && !load;
                output->action.value.type = compare ? BUSTER_TYPE_BOOL : store ? BUSTER_TYPE_VOID : value;
                if (arithmetic && !allowed)
                {
                    check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
                }
                else if (arithmetic && kind == BUSTER_TYPE_KIND_POINTER &&
                         !check_builtin_atomic_source_complete(table, type_element(table, value), input))
                {
                    check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
                }
                if (!load && output->action.diagnostic == CHECK_DIAG_NONE)
                {
                    if (compare)
                    {
                        const TypeId expected = type_pointer(table, value);
                        check_builtin_atomic_convert(table, input, output, values + 1, 1, expected);
                    }
                    const uint32_t stored = compare ? 2 : 1;
                    const TypeId operand = add_sub && kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_TYPE_LONG : value;
                    check_builtin_atomic_convert(table, input, output, values + stored, stored, operand);
                    output->store_value = values[stored];
                }
                if (init)
                {
                    output->success_order = 0;
                    output->known_orders = BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
                }
                else
                {
                    const uint32_t first_order = load ? 1 : compare ? 3 : 2;
                    for (uint32_t i = first_order; i < count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
                    {
                        check_builtin_atomic_convert(table, input, output, values + i, i, BUSTER_TYPE_INT);
                        if (values[i].constant_kind == CHECK_CONSTANT_INTEGER && values[i].fault == CHECK_FAULT_NONE &&
                            (values[i].flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                        {
                            const uint32_t order = (uint32_t)values[i].integer;
                            const bool failure = compare && i == 4;
                            const bool valid = order <= 5 && (!(load || failure) || (order != 3 && order != 4)) &&
                                               (!store || (order != 1 && order != 2 && order != 4));
                            if (failure)
                            {
                                output->failure_order = order;
                                output->known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER;
                            }
                            else
                            {
                                output->success_order = order;
                                output->known_orders |= BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
                            }
                            if (!valid && (values[i].flags & BUSTER_CHECK_ICE) != 0)
                            {
                                output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER;
                                output->warning_operands |= 1u << i;
                            }
                        }
                    }
                }
            }
        }
    }
    // A power of two up to the target's inline width (check_builtin_atomic_lock_free: 16 bytes with cx16).
    const bool constant_query = lock_free && output->action.diagnostic == CHECK_DIAG_NONE && values[0].constant_kind == CHECK_CONSTANT_INTEGER &&
                                values[0].fault == CHECK_FAULT_NONE &&
                                check_builtin_atomic_lock_free(table, input, CBUILTIN_C11_ATOMIC_IS_LOCK_FREE, (uint64_t)values[0].integer, NULL, false) == 1;
    output->action.value.constant_kind = constant_query ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
    output->action.value.integer = constant_query;
    output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
    if (constant_query && (values[0].flags & BUSTER_CHECK_ICE) != 0)
    {
        output->action.value.flags |= BUSTER_CHECK_ICE;
    }
}

// Whether a builtin is one of GNU's generic forms, whose values are pointers to them.
static bool check_builtin_atomic_generic(CBuiltinId id)
{
    return id == CBUILTIN_ATOMIC_LOAD || id == CBUILTIN_ATOMIC_STORE || id == CBUILTIN_ATOMIC_EXCHANGE || id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE;
}

// GNU's generic forms (#148), clang's LoadCopy, Copy, GNUXchg and GNUCmpXchg: __atomic_load (object, result, order),
// __atomic_store (object, value, order), __atomic_exchange (object, value, result, order) and __atomic_compare_exchange
// (object, expected, desired, weak, success, failure). The object is of any complete type of a size, but a function, an
// _Atomic or a _BitInt one (long double, records, unions and arrays among them), and not const but for a load. The value
// pointers convert to pointers to its unqualified type, but a store's value and a compare-exchange's desired one to the
// first argument's own pointer type, its qualifiers kept (clang's ByValType: a pointer to const passed for any of them
// is clang's discarded-qualifiers warning), the weak flag to bool and the orders to int, which an integer constant
// expression checks as the _n forms' are. No width or alignment is refused: the lowering calls libatomic's generic
// functions for what no native leaf takes, as clang's code calls them.
static void check_builtin_atomic_generic_one(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    *output = (CheckBuiltinAtomicResult){0};
    const CBuiltinId id = input->id;
    const bool load = id == CBUILTIN_ATOMIC_LOAD;
    const bool store = id == CBUILTIN_ATOMIC_STORE;
    const bool exchange = id == CBUILTIN_ATOMIC_EXCHANGE;
    const bool compare = id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE;
    const uint32_t count = compare ? 6 : exchange ? 4 : 3;
    CheckValue values[6] = {0};
    if (input->argument_count != count)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        output->runtime_builtin = id;
        output->action.value = (CheckValue){.type = compare ? BUSTER_TYPE_BOOL : BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT};
        output->evaluated_operands = (1u << count) - 1;
        output->compiler_barrier = true;
        for (uint32_t i = 0; i < count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_atomic_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        const bool pointer = check_action_kind(table, values[0].type) == BUSTER_TYPE_KIND_POINTER;
        const TypeId object = pointer ? type_element(table, type_strip_aligned(table, values[0].type)) : 0;
        const TypeId plain = pointer ? type_strip_aligned(table, object) : 0;
        const uint8_t kind = pointer ? type_kind(table, plain) : BUSTER_TYPE_KIND_VOID;
        if (output->action.diagnostic == CHECK_DIAG_NONE && !pointer)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && !load && (type_qualifiers(plain) & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && kind != BUSTER_TYPE_KIND_FUNCTION && !check_builtin_atomic_source_complete(table, object, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE &&
                 (kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_BIT_INT || type_size(table, object) == 0))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            const TypeId value = type_conversion_operand(table, object);
            const TypeId unqualified = type_pointer(table, value);
            const TypeId by_value = type_unqualified(type_strip_aligned(table, values[0].type));
            output->object_address = values[0];
            output->object_type = object;
            output->value_type = value;
            output->object_size = type_size(table, object);
            output->required_alignment = output->object_size;
            output->minimum_alignment = input->alignment_known ? input->minimum_alignment : 0;
            output->alignment_known = input->alignment_known;
            output->volatile_access = (type_qualifiers(plain) & BUSTER_TYPE_VOLATILE) != 0;
            output->reads_memory = !store;
            output->writes_memory = !load;
            output->rmw = exchange || compare;
            if (unqualified == 0)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 1);
            }
            // The value pointers, then the weak flag's and the orders' conversions.
            const uint32_t pointers = compare || exchange ? 3 : 2;
            for (uint32_t i = 1; i < count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
            {
                const bool keeps = (store && i == 1) || (exchange && i == 2) || (compare && i == 2);
                const TypeId destination = i >= pointers ? (compare && i == 3 ? BUSTER_TYPE_BOOL : BUSTER_TYPE_INT) : keeps ? by_value : unqualified;
                check_builtin_atomic_convert(table, input, output, values + i, i, destination);
            }
            output->store_value = values[store || exchange ? 1 : compare ? 2 : 0];
            for (uint32_t i = compare ? 4 : pointers; i < count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
            {
                if (values[i].constant_kind == CHECK_CONSTANT_INTEGER && values[i].fault == CHECK_FAULT_NONE && (values[i].flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                {
                    const uint32_t order = (uint32_t)values[i].integer;
                    const bool failure = compare && i == 5;
                    const bool valid = order <= 5 && (!(load || failure) || (order != 3 && order != 4)) && (!store || (order != 1 && order != 2 && order != 4));
                    output->failure_order = failure ? order : output->failure_order;
                    output->success_order = failure ? output->success_order : order;
                    output->known_orders |= failure ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER : BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER;
                    if (!valid && (values[i].flags & BUSTER_CHECK_ICE) != 0)
                    {
                        output->warnings |= BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER;
                        output->warning_operands |= 1u << i;
                    }
                }
            }
        }
    }
    output->action.value.constant_kind = CHECK_CONSTANT_NONE;
    output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
}

// The scoped families (#148), each its base builtin's operation with a scope after its arguments: clang's
// __scoped_atomic_* GNU's __atomic_* (__scoped_atomic_thread_fence an ordinary prototype's, void (int, int)), its
// __hip_atomic_* C11's on an object that isn't _Atomic, and its __opencl_atomic_* C11's (__opencl_atomic_init without a
// scope). x86-64 has the system's scope alone: clang's code generation switches over a scope that isn't a constant to the
// same code for each, so a scope converts to int, is checked against its family's model where it is an integer constant
// expression (SemaChecking's err_atomic_op_has_invalid_sync_scope: the generic scopes 0 to 5, HIP's 1 to 6, OpenCL's 1 to
// 4), and is evaluated and dropped.
typedef enum CheckBuiltinAtomicScope
{
    CHECK_BUILTIN_ATOMIC_SCOPE_NONE,
    CHECK_BUILTIN_ATOMIC_SCOPE_GENERIC,
    CHECK_BUILTIN_ATOMIC_SCOPE_HIP,
    CHECK_BUILTIN_ATOMIC_SCOPE_OPENCL,
} CheckBuiltinAtomicScope;

// The families' base builtins, in their catalog order.
static const CBuiltinId check_builtin_atomic_scoped_bases[] = {
    CBUILTIN_ATOMIC_ADD_FETCH, CBUILTIN_ATOMIC_AND_FETCH, CBUILTIN_ATOMIC_COMPARE_EXCHANGE, CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N,
    CBUILTIN_ATOMIC_EXCHANGE, CBUILTIN_ATOMIC_EXCHANGE_N, CBUILTIN_ATOMIC_FETCH_ADD, CBUILTIN_ATOMIC_FETCH_AND,
    CBUILTIN_ATOMIC_FETCH_FMAXIMUM, CBUILTIN_ATOMIC_FETCH_FMAXIMUM_NUM, CBUILTIN_ATOMIC_FETCH_FMINIMUM, CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM,
    CBUILTIN_ATOMIC_FETCH_MAX, CBUILTIN_ATOMIC_FETCH_MIN, CBUILTIN_ATOMIC_FETCH_NAND, CBUILTIN_ATOMIC_FETCH_OR,
    CBUILTIN_ATOMIC_FETCH_SUB, CBUILTIN_ATOMIC_FETCH_UDEC, CBUILTIN_ATOMIC_FETCH_UINC, CBUILTIN_ATOMIC_FETCH_XOR,
    CBUILTIN_ATOMIC_LOAD, CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_MAX_FETCH, CBUILTIN_ATOMIC_MIN_FETCH,
    CBUILTIN_ATOMIC_NAND_FETCH, CBUILTIN_ATOMIC_OR_FETCH, CBUILTIN_ATOMIC_STORE, CBUILTIN_ATOMIC_STORE_N,
    CBUILTIN_ATOMIC_SUB_FETCH, CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_XOR_FETCH,
};
static const CBuiltinId check_builtin_atomic_hip_bases[] = {
    CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG, CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK, CBUILTIN_C11_ATOMIC_EXCHANGE,
    CBUILTIN_C11_ATOMIC_FETCH_ADD, CBUILTIN_C11_ATOMIC_FETCH_AND, CBUILTIN_C11_ATOMIC_FETCH_MAX, CBUILTIN_C11_ATOMIC_FETCH_MIN,
    CBUILTIN_C11_ATOMIC_FETCH_OR, CBUILTIN_C11_ATOMIC_FETCH_SUB, CBUILTIN_C11_ATOMIC_FETCH_XOR, CBUILTIN_C11_ATOMIC_LOAD,
    CBUILTIN_C11_ATOMIC_STORE,
};
static const CBuiltinId check_builtin_atomic_opencl_bases[] = {
    CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG, CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK, CBUILTIN_C11_ATOMIC_EXCHANGE,
    CBUILTIN_C11_ATOMIC_FETCH_ADD, CBUILTIN_C11_ATOMIC_FETCH_AND, CBUILTIN_C11_ATOMIC_FETCH_MAX, CBUILTIN_C11_ATOMIC_FETCH_MIN,
    CBUILTIN_C11_ATOMIC_FETCH_OR, CBUILTIN_C11_ATOMIC_FETCH_SUB, CBUILTIN_C11_ATOMIC_FETCH_XOR, CBUILTIN_C11_ATOMIC_INIT,
    CBUILTIN_C11_ATOMIC_LOAD, CBUILTIN_C11_ATOMIC_STORE,
};
_Static_assert(BUSTER_ARRAY_LENGTH(check_builtin_atomic_scoped_bases) == CBUILTIN_SCOPED_ATOMIC_XOR_FETCH - CBUILTIN_SCOPED_ATOMIC_ADD_FETCH + 1 &&
                   BUSTER_ARRAY_LENGTH(check_builtin_atomic_hip_bases) == CBUILTIN_HIP_ATOMIC_STORE - CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_STRONG + 1 &&
                   BUSTER_ARRAY_LENGTH(check_builtin_atomic_opencl_bases) == CBUILTIN_OPENCL_ATOMIC_STORE - CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG + 1,
               "a base for each scoped builtin");

// A builtin's scope model: CHECK_BUILTIN_ATOMIC_SCOPE_NONE for one that takes no scope, __opencl_atomic_init among them.
static CheckBuiltinAtomicScope check_builtin_atomic_scope(CBuiltinId id)
{
    CheckBuiltinAtomicScope result = CHECK_BUILTIN_ATOMIC_SCOPE_NONE;
    if (id >= CBUILTIN_SCOPED_ATOMIC_ADD_FETCH && id <= CBUILTIN_SCOPED_ATOMIC_XOR_FETCH)
    {
        result = CHECK_BUILTIN_ATOMIC_SCOPE_GENERIC;
    }
    else if (id >= CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_STRONG && id <= CBUILTIN_HIP_ATOMIC_STORE)
    {
        result = CHECK_BUILTIN_ATOMIC_SCOPE_HIP;
    }
    else if (id >= CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG && id <= CBUILTIN_OPENCL_ATOMIC_STORE && id != CBUILTIN_OPENCL_ATOMIC_INIT)
    {
        result = CHECK_BUILTIN_ATOMIC_SCOPE_OPENCL;
    }
    return result;
}

// The base builtin whose operation a scoped family's builtin is, or any other builtin itself.
static CBuiltinId check_builtin_atomic_base(CBuiltinId id)
{
    CBuiltinId result = id;
    if (id >= CBUILTIN_SCOPED_ATOMIC_ADD_FETCH && id <= CBUILTIN_SCOPED_ATOMIC_XOR_FETCH)
    {
        result = check_builtin_atomic_scoped_bases[id - CBUILTIN_SCOPED_ATOMIC_ADD_FETCH];
    }
    else if (id >= CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_STRONG && id <= CBUILTIN_HIP_ATOMIC_STORE)
    {
        result = check_builtin_atomic_hip_bases[id - CBUILTIN_HIP_ATOMIC_COMPARE_EXCHANGE_STRONG];
    }
    else if (id >= CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG && id <= CBUILTIN_OPENCL_ATOMIC_STORE)
    {
        result = check_builtin_atomic_opencl_bases[id - CBUILTIN_OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG];
    }
    return result;
}

// Whether the checker takes an atomic builtin (check_graph_builtin_op's family): C11's, the loads', stores', fences' and
// lock-free queries', a read/modify/write, GNU's generic forms, and a scoped family's whose base it takes.
static __attribute__((unused)) bool check_builtin_atomic_checked(CBuiltinId id)
{
    const CBuiltinId base = check_builtin_atomic_base(id);
    const bool result = check_builtin_atomic_c11(base) || base == CBUILTIN_ATOMIC_LOAD_N || base == CBUILTIN_ATOMIC_STORE_N || base == CBUILTIN_SYNC_SYNCHRONIZE ||
                        base == CBUILTIN_ATOMIC_THREAD_FENCE || base == CBUILTIN_ATOMIC_SIGNAL_FENCE || base == CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE ||
                        base == CBUILTIN_ATOMIC_IS_LOCK_FREE || check_builtin_atomic_generic(base) || check_builtin_atomic_rmw_rule(base) != NULL;
    return result;
}

// An atomic builtin's checking by its family, which the source checker's graph and the declaration-time evaluator
// (c_check_builtin_constant.c) both run: C11's, a read/modify/write's (with the orders' ICE proofs from the source's own
// analysis), GNU's generic forms', or the loads, stores, fences and lock-free queries'. A scoped family's builtin is its
// base's (check_builtin_atomic_base) on all but its last argument, its scope: converted to int, checked against its model
// where it is an integer constant expression (but __scoped_atomic_thread_fence's, an ordinary int), and evaluated after
// the base's arguments.
static __attribute__((unused)) void check_builtin_atomic_family(TypeTable* table, const CheckBuiltinAtomicAction* input, CheckBuiltinAtomicResult* output)
{
    const CheckBuiltinAtomicScope scope = check_builtin_atomic_scope(input->id);
    const uint32_t scoped = scope != CHECK_BUILTIN_ATOMIC_SCOPE_NONE && input->argument_count != 0;
    CheckBuiltinAtomicAction base = *input;
    base.id = check_builtin_atomic_base(input->id);
    base.argument_count = input->argument_count - scoped;
    base.plain_object = scope == CHECK_BUILTIN_ATOMIC_SCOPE_HIP;
    if (scope != CHECK_BUILTIN_ATOMIC_SCOPE_NONE && input->argument_count == 0)
    {
        *output = (CheckBuiltinAtomicResult){0};
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (check_builtin_atomic_c11(base.id))
    {
        check_builtin_atomic_c11_one(table, &base, output);
    }
    else if (check_builtin_atomic_rmw_rule(base.id) != NULL)
    {
        const CheckBuiltinAtomicRmwAction rmw = {
            .source = base,
            .order_ice_arguments = check_builtin_atomic_rmw_order_ice(base.id, base.arguments, base.argument_count),
        };
        CheckBuiltinAtomicRmwResult full;
        check_builtin_atomic_rmw(table, &rmw, &full, 1);
        *output = full.atomic;
    }
    else if (check_builtin_atomic_generic(base.id))
    {
        check_builtin_atomic_generic_one(table, &base, output);
    }
    else
    {
        check_builtin_atomic(table, &base, output, 1);
    }
    if (scoped != 0 && output->action.diagnostic == CHECK_DIAG_NONE)
    {
        const uint32_t index = input->argument_count - 1;
        CheckValue value = check_builtin_atomic_argument(table, input, output, index);
        check_builtin_atomic_convert(table, input, output, &value, index, BUSTER_TYPE_INT);
        check_action_inherit(&output->action.value, &value);
        const uint32_t model = (uint32_t)value.integer;
        const bool valid = scope == CHECK_BUILTIN_ATOMIC_SCOPE_GENERIC ? model <= 5 :
                           scope == CHECK_BUILTIN_ATOMIC_SCOPE_HIP     ? model >= 1 && model <= 6 :
                                                                         model >= 1 && model <= 4;
        if (output->action.diagnostic == CHECK_DIAG_NONE && input->id != CBUILTIN_SCOPED_ATOMIC_THREAD_FENCE && (value.flags & BUSTER_CHECK_ICE) != 0 &&
            value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && !valid)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
        }
        output->evaluated_operands |= UINT32_C(1) << index;
    }
}

#endif
