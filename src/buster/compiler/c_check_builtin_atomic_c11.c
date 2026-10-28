// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C11 atomic source constraints and the common atomic-effect handoff. Unlike GNU's _n forms, the object is an
// _Atomic(T), and load/store/exchange/compare-exchange also take records and extended scalar representations.
// Argument identities remain in source order; the lowering supplies init's order and compare-exchange's weak flag.
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
            if (!pointer || type_kind(table, atomic) != BUSTER_TYPE_KIND_ATOMIC)
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
    const bool constant_query = lock_free && output->action.diagnostic == CHECK_DIAG_NONE && values[0].constant_kind == CHECK_CONSTANT_INTEGER &&
                                values[0].fault == CHECK_FAULT_NONE && values[0].integer <= 8 &&
                                (values[0].integer == 0 || (values[0].integer & (values[0].integer - 1)) == 0);
    output->action.value.constant_kind = constant_query ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
    output->action.value.integer = constant_query;
    output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
    if (constant_query && (values[0].flags & BUSTER_CHECK_ICE) != 0)
    {
        output->action.value.flags |= BUSTER_CHECK_ICE;
    }
}

#endif
