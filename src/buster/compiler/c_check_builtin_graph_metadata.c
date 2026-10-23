// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact, padding-independent metadata comparisons shared by graph tests and extracted-body/corpus proofs.
// The value comparator supplies the harness's floating/NaN policy; production checking does not call these.

static __attribute__((unused)) bool check_builtin_call_equal(const CheckBuiltinCall* a, const CheckBuiltinCall* b)
{
    bool equal = a->node == b->node && a->nan_payload_length == b->nan_payload_length && a->nan_payload_known == b->nan_payload_known &&
                 a->string_literal_start == b->string_literal_start && a->absolute_address_constant == b->absolute_address_constant &&
                 a->minimum_alignment == b->minimum_alignment && a->alignment_known == b->alignment_known;
    if (equal && a->nan_payload_known && a->nan_payload_length != 0)
    {
        equal = a->nan_payload == NULL ? b->nan_payload == NULL : b->nan_payload != NULL && memcmp(a->nan_payload, b->nan_payload, a->nan_payload_length) == 0;
    }
    for (uint32_t i = 0; equal && i < 4; i += 1)
    {
        equal = a->object_size_known[i] == b->object_size_known[i] && a->object_sizes[i] == b->object_sizes[i];
    }
    return equal;
}

static __attribute__((unused)) bool check_builtin_misc_effect_equal(const CheckBuiltinMiscEffect* a, const CheckBuiltinMiscEffect* b,
                                                                  bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    return a->node == b->node && value_equal(&a->constant_value, &b->constant_value) &&
           a->constant_evaluated_operands == b->constant_evaluated_operands && a->evaluated_operands == b->evaluated_operands &&
           a->warnings == b->warnings && a->unsupported == b->unsupported && a->frame_level == b->frame_level &&
           a->alignment == b->alignment && a->alignment_offset == b->alignment_offset && a->runtime_builtin == b->runtime_builtin &&
           a->constraint == b->constraint && a->warning_operands == b->warning_operands && a->prefetch_rw == b->prefetch_rw &&
           a->prefetch_locality == b->prefetch_locality && a->terminates == b->terminates && a->reads_memory == b->reads_memory &&
           a->writes_memory == b->writes_memory && a->allocates_stack == b->allocates_stack && a->alignment_offset_known == b->alignment_offset_known;
}

static __attribute__((unused)) bool check_builtin_float_effect_equal(const CheckBuiltinFloatEffect* a, const CheckBuiltinFloatEffect* b)
{
    return a->node == b->node && a->floating_type == b->floating_type && a->evaluated_operands == b->evaluated_operands &&
           a->definite_operands == b->definite_operands && a->selected_selector == b->selected_selector && a->unsupported == b->unsupported &&
           a->warnings == b->warnings && a->runtime_builtin == b->runtime_builtin && a->evaluation == b->evaluation &&
           a->folded == b->folded && a->reads_memory == b->reads_memory;
}

static __attribute__((unused)) bool check_builtin_query_effect_equal(const CheckBuiltinQueryEffect* a, const CheckBuiltinQueryEffect* b)
{
    return a->node == b->node && a->evaluated_operands == b->evaluated_operands && a->warnings == b->warnings &&
           a->warning_operands == b->warning_operands && a->mode == b->mode && a->known == b->known && a->constraint == b->constraint;
}

static __attribute__((unused)) bool check_builtin_atomic_effect_equal(const CheckBuiltinAtomicEffect* a, const CheckBuiltinAtomicEffect* b,
                                                                    bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    return a->node == b->node && value_equal(&a->object_address, &b->object_address) && value_equal(&a->store_value, &b->store_value) &&
           a->object_type == b->object_type && a->value_type == b->value_type && a->object_size == b->object_size &&
           a->required_alignment == b->required_alignment && a->minimum_alignment == b->minimum_alignment &&
           a->evaluated_operands == b->evaluated_operands && a->success_order == b->success_order && a->failure_order == b->failure_order &&
           a->known_orders == b->known_orders && a->warnings == b->warnings && a->warning_operands == b->warning_operands &&
           a->unsupported == b->unsupported && a->runtime_builtin == b->runtime_builtin && a->fence_scope == b->fence_scope &&
           a->alignment_known == b->alignment_known && a->volatile_access == b->volatile_access && a->reads_memory == b->reads_memory &&
           a->writes_memory == b->writes_memory && a->rmw == b->rmw && a->compiler_barrier == b->compiler_barrier;
}

static __attribute__((unused)) bool check_builtin_stream_metadata_equal(const CheckStream* a, const CheckStream* b,
                                                                      bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    bool equal = a->misc_count == b->misc_count && a->floating_count == b->floating_count && a->query_count == b->query_count && a->atomic_count == b->atomic_count;
    for (uint32_t i = 0; equal && i < a->misc_count; i += 1)
    {
        equal = check_builtin_misc_effect_equal(a->misc + i, b->misc + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->floating_count; i += 1)
    {
        equal = check_builtin_float_effect_equal(a->floating + i, b->floating + i);
    }
    for (uint32_t i = 0; equal && i < a->query_count; i += 1)
    {
        equal = check_builtin_query_effect_equal(a->queries + i, b->queries + i);
    }
    for (uint32_t i = 0; equal && i < a->atomic_count; i += 1)
    {
        equal = check_builtin_atomic_effect_equal(a->atomics + i, b->atomics + i, value_equal);
    }
    return equal;
}
