// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact, padding-independent metadata comparisons shared by graph tests and extracted-body/corpus proofs.
// The value comparator supplies the harness's floating/NaN policy; production checking does not call these.

static __attribute__((unused)) bool check_builtin_call_equal(const CheckBuiltinCall* a, const CheckBuiltinCall* b)
{
    bool equal = a->node == b->node && a->nan_payload_length == b->nan_payload_length && a->nan_payload_known == b->nan_payload_known &&
                 a->string_literal_start == b->string_literal_start && a->absolute_address_constant == b->absolute_address_constant &&
                 a->complex_constant == b->complex_constant && a->constant_p_side_effects == b->constant_p_side_effects &&
                 a->minimum_alignment == b->minimum_alignment && a->alignment_known == b->alignment_known;
    if (equal && a->nan_payload_known && a->nan_payload_length != 0)
    {
        equal = a->nan_payload == NULL ? b->nan_payload == NULL : b->nan_payload != NULL && memcmp(a->nan_payload, b->nan_payload, a->nan_payload_length) == 0;
    }
    for (uint32_t i = 0; equal && i < 4; i += 1)
    {
        equal = a->object_size_known[i] == b->object_size_known[i] &&
                a->object_size_source_constant[i] == b->object_size_source_constant[i] && a->object_sizes[i] == b->object_sizes[i];
    }
    return equal;
}

static __attribute__((unused)) bool check_store_effect_equal(const CheckStoreEffect* a, const CheckStoreEffect* b,
                                                           bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    return a->node == b->node && value_equal(&a->value, &b->value) && value_equal(&a->destination, &b->destination) &&
           a->runtime_builtin == b->runtime_builtin && a->evaluated_operands == b->evaluated_operands &&
           a->warnings == b->warnings && a->warning_operands == b->warning_operands && a->unsupported == b->unsupported;
}

static __attribute__((unused)) bool check_x86_call_equal(const CheckX86Call* a, const CheckX86Call* b)
{
    return a->node == b->node && a->info == b->info && a->required_ice_arguments == b->required_ice_arguments &&
           a->ice_arguments == b->ice_arguments && a->reachability == b->reachability &&
           a->constant_evaluated_context == b->constant_evaluated_context &&
           a->runtime_diagnostic_suppressed == b->runtime_diagnostic_suppressed;
}

static __attribute__((unused)) bool check_builtin_x86_effect_equal(const CheckBuiltinX86Effect* a, const CheckBuiltinX86Effect* b,
                                                                 bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    bool equal = a->node == b->node && a->info == b->info && a->status == b->status && a->constraint == b->constraint &&
                 a->checked_ice_arguments == b->checked_ice_arguments && a->matched_rules == b->matched_rules &&
                 a->unsupported == b->unsupported && a->range_event_count == b->range_event_count;
    for (uint32_t i = 0; equal && i < a->range_event_count; i += 1)
    {
        const CheckBuiltinX86RangeEvent* x = a->range_events + i;
        const CheckBuiltinX86RangeEvent* y = b->range_events + i;
        equal = x->node == y->node && x->argument == y->argument && x->low == y->low && x->high == y->high &&
                x->source_line == y->source_line && value_equal(&x->value, &y->value) && x->disposition == y->disposition &&
                x->default_error == y->default_error;
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

static __attribute__((unused)) bool check_builtin_float_effect_equal(const CheckBuiltinFloatEffect* a, const CheckBuiltinFloatEffect* b,
                                                                   bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    return a->node == b->node && a->math_kind == b->math_kind && a->errno_pointer_type == b->errno_pointer_type &&
           value_equal(a->converted_arguments, b->converted_arguments) && value_equal(a->converted_arguments + 1, b->converted_arguments + 1) &&
           a->floating_type == b->floating_type && a->evaluated_operands == b->evaluated_operands &&
           a->definite_operands == b->definite_operands && a->selected_selector == b->selected_selector && a->unsupported == b->unsupported &&
           a->warnings == b->warnings && a->runtime_builtin == b->runtime_builtin && a->evaluation == b->evaluation &&
           a->folded == b->folded && a->reads_memory == b->reads_memory &&
           a->compiler_barrier == b->compiler_barrier && a->reads_fp_environment == b->reads_fp_environment &&
           a->may_raise_fp_exceptions == b->may_raise_fp_exceptions && a->may_raise_inexact == b->may_raise_inexact && a->may_set_errno == b->may_set_errno;
}

static __attribute__((unused)) bool check_builtin_query_effect_equal(const CheckBuiltinQueryEffect* a, const CheckBuiltinQueryEffect* b,
                                                                   bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    return a->node == b->node && value_equal(&a->runtime_value, &b->runtime_value) &&
           a->evaluated_operands == b->evaluated_operands && a->warnings == b->warnings &&
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

static __attribute__((unused)) bool check_builtin_vector_effect_equal(const CheckBuiltinVectorEffect* a, const CheckBuiltinVectorEffect* b,
                                                                    bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    bool equal = a->node == b->node && a->element_type == b->element_type && a->memory_type == b->memory_type &&
                 a->lane_count == b->lane_count && a->evaluated_argument_count == b->evaluated_argument_count &&
                 a->warnings == b->warnings && a->warning_operands == b->warning_operands && a->unsupported == b->unsupported &&
                 a->runtime_builtin == b->runtime_builtin && a->constraint == b->constraint && a->store_conversion == b->store_conversion &&
                 a->reads_memory == b->reads_memory && a->writes_memory == b->writes_memory && a->nondeterministic == b->nondeterministic &&
                 a->constant_evaluation_unmodeled == b->constant_evaluation_unmodeled && a->reads_fp_environment == b->reads_fp_environment &&
                 a->may_raise_fp_exceptions == b->may_raise_fp_exceptions && a->bool_splat_signed_true == b->bool_splat_signed_true;
    for (uint32_t i = 0; equal && i < 3; i += 1)
    {
        equal = value_equal(a->converted_arguments + i, b->converted_arguments + i);
    }
    return equal;
}

static __attribute__((unused)) bool check_builtin_stream_metadata_equal(const CheckStream* a, const CheckStream* b,
                                                                      bool (*value_equal)(const CheckValue*, const CheckValue*))
{
    bool equal = a->misc_count == b->misc_count && a->floating_count == b->floating_count && a->query_count == b->query_count &&
                 a->atomic_count == b->atomic_count && a->vector_count == b->vector_count && a->x86_count == b->x86_count;
    for (uint32_t i = 0; equal && i < a->misc_count; i += 1)
    {
        equal = check_builtin_misc_effect_equal(a->misc + i, b->misc + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->floating_count; i += 1)
    {
        equal = check_builtin_float_effect_equal(a->floating + i, b->floating + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->query_count; i += 1)
    {
        equal = check_builtin_query_effect_equal(a->queries + i, b->queries + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->atomic_count; i += 1)
    {
        equal = check_builtin_atomic_effect_equal(a->atomics + i, b->atomics + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->vector_count; i += 1)
    {
        equal = check_builtin_vector_effect_equal(a->vectors + i, b->vectors + i, value_equal);
    }
    for (uint32_t i = 0; equal && i < a->x86_count; i += 1)
    {
        equal = check_builtin_x86_effect_equal(a->x86 + i, b->x86 + i, value_equal);
    }
    return equal;
}
