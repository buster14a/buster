// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's constant_p and object_size semantic queries. Include after c_check_builtin_misc.c. The producer supplies
// already elaborated operands, immutable source-position tag completions, and explicit AST/object-bound evidence.
// Checking never executes operands or reads source memory. Source query values stay separate from runtime plans;
// object_size and constant_p SKIP plans suppress operand effects/faults. Invalid source types and arity diagnose.
//
// constant_p_source_proof certifies the source evaluator's actual context. Its ZERO/ONE result is independent of
// constant_p_runtime_plan and constant_p_runtime_result. Successful speculative folding precedes Clang's syntactic
// HasSideEffects fallback: a selected constant can fold despite an unchosen call/increment. An explicit source
// proof therefore has no universal syntactic effect/fault veto. INFER retains the old guarded value/origin inference
// only for context-independent actions. A context-sensitive action needs BOTH explicit proof and runtime plan;
// otherwise it publishes UNRESOLVED, never a guessed runtime zero/one or a guessed unevaluated operand.
//
// Explicit runtime effects describe actual context-pruned operand execution, including nested query plans, not
// all syntactic operands and not the source evaluator's speculative faults. SKIP suppresses them; EVALUATE plans
// evaluate operand zero exactly once and retain the summary even with a known answer. FP-environment/exception
// requirements imply SIDE_EFFECT. Source action.value remains clean ICE independently; consumers MUST consult the
// runtime plan/value/effects before emitting or eliminating the query. EVALUATE_DEFERRED needs a compiler-internal
// constantness test, not a CPU test; UNRESOLVED has no execution plan. Both retain explicit unsupported reasons.
// Original exact Clang23.1.1 baseline-O0 observations: source1/runtime0 with inexact FP work, nested source1/runtime1
// retaining that FP work, and source1/runtimeSKIP0 for an unchosen-call inexact operand. Other compiler profiles need
// their own producer proof. This interface does not infer runtime effects from numeric payloads or host FP state.
//
// string_literal_start certifies that the speculative value denotes the FIRST character of an actual string
// literal, including that pointer cast to an integer. It is proof about the final operand, not an object-ID convention
// or a guess from a char pointer. This witness remains usable when a symbolic pointer-to-integer cast has lost its
// CheckValue constant representation. Interior literal pointers and addresses of ordinary objects have no witness.
// absolute_address_constant separately certifies a folded absolute-address origin, including a pointer-to-integer
// cast whose CheckValue representation was lost. The producer must prove the full expression has no faults/effects;
// neither witness may be inferred from a nonconstant value's default object-zero fields. The action also checks
// the operand's effect/fault columns before using either witness.
// complex_constant certifies speculative foldability of the actual final complex operand. It carries no numeric
// payload and is used only for complex types, with the same fault/effect guards. The producer must establish this
// proof from the expression, never from its complex type alone or a discarded component.
//
// object_size_known[m] / object_sizes[m] are the producer's independently computed bound for mode m: whole-object
// maximum, subobject maximum, whole-object minimum, subobject minimum. No pointee sizeof, allocation extent or
// symbolic-object origin is inferred here. object_size_source_constant[m] separately certifies that the selected
// bound is available to Clang-compatible source constant evaluation. A runtime-known bound is insufficient:
// a string literal can have a runtime whole-object bound while only its subobject bound is source-constant.
// The producer may establish a source bound while ignoring operand effects. Unknown modes 0/1 use
// ULONG_MAX and modes 2/3 zero as lowering fallbacks. A known zero remains distinct from an unknown zero.
//
// Certified constant_p answers, source-proven bounds and unknown effectful pointers yield clean source constants. Other
// object_size results are deferred by Clang's source evaluator: action.value is nonconstant and permits
// an unevaluated ICE arm, while runtime_value retains the bound or conservative sentinel for lowering. Neither path
// executes object_size operands. Source argument conversions
// remain available for diagnostics/graph bookkeeping; they do not authorize runtime evaluation. Actions/results and
// argument/tag slices must be disjoint from each other and mutable type storage, with ordinary C alignment and no
// padding. The ordered bounded batch may intern decay/pointer types. Zero count permits null slices. This module
// alone establishes no source-dispatch/catalog capability or object-bound analysis acceptance.
//
// Rules were checked against Clang's original sources, without importing implementation:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/AST/ExprConstant.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp
// https://clang.llvm.org/docs/LanguageExtensions.html#evaluating-object-size
#ifndef BUSTER_C_CHECK_BUILTIN_QUERY_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_QUERY_INCLUDED

#define BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_RUNTIME_PLAN 0x01u
#define BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_CONSTANT_TEST 0x02u
#define BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_SOURCE_PROOF 0x04u

typedef enum CheckBuiltinQueryProof
{
    CHECK_BUILTIN_QUERY_PROOF_INFER,
    CHECK_BUILTIN_QUERY_PROOF_ZERO,
    CHECK_BUILTIN_QUERY_PROOF_ONE,
    CHECK_BUILTIN_QUERY_PROOF_DEFERRED,
} CheckBuiltinQueryProof;

typedef enum CheckBuiltinQueryRuntimePlan
{
    CHECK_BUILTIN_QUERY_RUNTIME_INFER,
    CHECK_BUILTIN_QUERY_RUNTIME_SKIP,
    CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN,
    CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED,
    CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED,
} CheckBuiltinQueryRuntimePlan;

typedef struct CheckBuiltinQueryEffects CheckBuiltinQueryEffects;
struct CheckBuiltinQueryEffects
{
    uint32_t flags;
    CheckFault fault;
    uint32_t fault_node;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
};

typedef struct CheckBuiltinQueryAction CheckBuiltinQueryAction;
struct CheckBuiltinQueryAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    bool string_literal_start;
    bool absolute_address_constant;
    bool complex_constant;
    bool object_size_known[4];
    bool object_size_source_constant[4];
    uint64_t object_sizes[4];
    CheckBuiltinQueryProof constant_p_source_proof;
    CheckBuiltinQueryRuntimePlan constant_p_runtime_plan;
    uint8_t constant_p_runtime_result;
    CheckBuiltinQueryEffects constant_p_runtime_effects;
    bool constant_p_context_sensitive;
};

typedef struct CheckBuiltinQueryResult CheckBuiltinQueryResult;
struct CheckBuiltinQueryResult
{
    CheckActionResult action;
    // Lowering fallback, kept separate so a runtime sentinel never becomes a source initializer/ICE.
    CheckValue runtime_value;
    CheckConversion argument_conversions[2];
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t mode;
    // For constant_p, the boolean answer is determined. For object_size, a real selected bound was known.
    bool known;
    CheckBuiltinMiscConstraint constraint;
    CheckBuiltinQueryProof source_proof;
    CheckBuiltinQueryRuntimePlan runtime_plan;
    CheckBuiltinQueryEffects runtime_effects;
    uint32_t unsupported;
};

static bool check_builtin_query_effects_empty(const CheckBuiltinQueryEffects* effects)
{
    const bool result = effects->flags == 0 && effects->fault == CHECK_FAULT_NONE && effects->fault_node == 0 &&
        !effects->reads_fp_environment && !effects->may_raise_fp_exceptions;
    return result;
}

static bool check_builtin_query_proof_shape(const CheckBuiltinQueryAction* input)
{
    const CheckBuiltinQueryRuntimePlan plan = input->constant_p_runtime_plan;
    const CheckBuiltinQueryEffects* effects = &input->constant_p_runtime_effects;
    const bool known_plan = plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP || plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
    const bool result = (uint32_t)input->constant_p_source_proof <= CHECK_BUILTIN_QUERY_PROOF_DEFERRED &&
        (uint32_t)plan <= CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED && input->constant_p_runtime_result <= 1 &&
        (known_plan || input->constant_p_runtime_result == 0) && (effects->flags & ~BUSTER_CHECK_SIDE_EFFECT) == 0 &&
        (uint32_t)effects->fault <= CHECK_FAULT_UNSUPPORTED_CONSTANT && (effects->fault != CHECK_FAULT_NONE || effects->fault_node == 0);
    return result;
}

static bool check_builtin_query_constant(const TypeTable* table, const CheckBuiltinQueryAction* input, const CheckValue* value)
{
    const uint8_t kind = check_action_kind(table, value->type);
    const bool integer = check_action_is_integer(table, value->type);
    const bool pointer = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR;
    bool result = false;
    if (value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
    {
        if (integer && value->constant_kind == CHECK_CONSTANT_INTEGER)
        {
            result = true;
        }
        else if (kind == BUSTER_TYPE_KIND_FLOATING && value->constant_kind == CHECK_CONSTANT_FLOATING)
        {
            result = true;
        }
        else if (kind == BUSTER_TYPE_KIND_COMPLEX && input->complex_constant)
        {
            result = true;
        }
        else if ((integer || pointer) && value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object == 0)
        {
            // Object zero is already the CheckValue contract for an absolute address, including null.
            result = true;
        }
        else if ((integer || pointer) && input->absolute_address_constant)
        {
            result = value->constant_kind != CHECK_CONSTANT_ADDRESS || value->object == 0;
        }
        else if ((integer || pointer) && input->string_literal_start)
        {
            // The producer, which owns the AST and cast provenance, attests this special address origin.
            result = value->constant_kind != CHECK_CONSTANT_ADDRESS || value->offset == 0;
        }
    }
    return result;
}

static void check_builtin_query_constant_plan(const TypeTable* table, const CheckBuiltinQueryAction* input, const CheckValue* operand, CheckBuiltinQueryResult* output)
{
    CheckBuiltinQueryProof source = input->constant_p_source_proof;
    CheckBuiltinQueryRuntimePlan plan = input->constant_p_runtime_plan;
    const bool legacy = !input->constant_p_context_sensitive && source == CHECK_BUILTIN_QUERY_PROOF_INFER &&
        plan == CHECK_BUILTIN_QUERY_RUNTIME_INFER && check_builtin_query_effects_empty(&input->constant_p_runtime_effects);
    if (source == CHECK_BUILTIN_QUERY_PROOF_INFER)
    {
        source = input->constant_p_context_sensitive ? CHECK_BUILTIN_QUERY_PROOF_DEFERRED :
            check_builtin_query_constant(table, input, operand) ? CHECK_BUILTIN_QUERY_PROOF_ONE : CHECK_BUILTIN_QUERY_PROOF_ZERO;
    }
    if (plan == CHECK_BUILTIN_QUERY_RUNTIME_INFER ||
        (input->constant_p_context_sensitive && input->constant_p_source_proof == CHECK_BUILTIN_QUERY_PROOF_INFER))
    {
        plan = legacy ? CHECK_BUILTIN_QUERY_RUNTIME_SKIP : CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED;
    }
    output->source_proof = source;
    output->runtime_plan = plan;
    output->known = source != CHECK_BUILTIN_QUERY_PROOF_DEFERRED;
    output->action.value = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    if (output->known)
    {
        output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
        output->action.value.integer = source == CHECK_BUILTIN_QUERY_PROOF_ONE;
        output->action.value.flags = BUSTER_CHECK_ICE;
    }
    else
    {
        output->unsupported |= BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_SOURCE_PROOF;
    }
    output->runtime_value = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    if (plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP || plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN)
    {
        output->runtime_value.constant_kind = CHECK_CONSTANT_INTEGER;
        output->runtime_value.integer = legacy ? source == CHECK_BUILTIN_QUERY_PROOF_ONE : input->constant_p_runtime_result;
        output->runtime_value.flags = legacy ? BUSTER_CHECK_ICE : 0;
    }
    if (plan != CHECK_BUILTIN_QUERY_RUNTIME_SKIP)
    {
        output->runtime_effects = input->constant_p_runtime_effects;
        if (output->runtime_effects.reads_fp_environment || output->runtime_effects.may_raise_fp_exceptions)
        {
            output->runtime_effects.flags |= BUSTER_CHECK_SIDE_EFFECT;
        }
        output->runtime_value.flags = output->runtime_effects.flags;
        output->runtime_value.fault = output->runtime_effects.fault;
        output->runtime_value.fault_node = output->runtime_effects.fault_node;
    }
    if (plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN || plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED)
    {
        output->evaluated_operands = 1;
    }
    if (plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED)
    {
        output->unsupported |= BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_CONSTANT_TEST;
    }
    else if (plan == CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED)
    {
        output->unsupported |= BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_RUNTIME_PLAN;
    }
}

static __attribute__((unused)) void check_builtin_query_one(TypeTable* table, const CheckBuiltinQueryAction* input, CheckBuiltinQueryResult* output)
{
    *output = (CheckBuiltinQueryResult){ 0 };
    const bool constant = input->id == CBUILTIN_CONSTANT_P;
    // Dynamic queries reuse certified static bounds where available, otherwise the documented unknown sentinel.
    // No dynamic allocation/VLA extent is inferred from a pointer's type or from an unevaluated side effect.
    const bool object_size = input->id == CBUILTIN_OBJECT_SIZE || input->id == CBUILTIN_DYNAMIC_OBJECT_SIZE;
    const uint32_t expected = constant ? 1 : 2;
    if (!constant && !object_size)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != expected)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (constant && !check_builtin_query_proof_shape(input))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else if (input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        const CheckBuiltinMiscAction arguments =
        {
            .id = input->id,
            .arguments = input->arguments,
            .argument_count = input->argument_count,
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        CheckBuiltinMiscResult checked = { 0 };
        CheckValue values[2] = { 0 };
        for (uint32_t i = 0; i < expected && checked.action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_misc_argument(table, &arguments, &checked, i);
        }
        uint64_t value = 0;
        bool known = false;
        bool deferred = false;
        if (checked.action.diagnostic == CHECK_DIAG_NONE && !constant)
        {
            const TypeId pointer = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST);
            const bool pointer_ok = check_builtin_misc_convert(table, &arguments, &checked, values, 0, pointer);
            const bool original_integer = check_action_is_integer(table, values[1].type);
            const bool mode_ok = pointer_ok && check_builtin_misc_convert(table, &arguments, &checked, values + 1, 1, BUSTER_TYPE_INT);
            if (mode_ok && !original_integer)
            {
                // An implicit floating-to-int parameter conversion does not turn the original expression into ICE.
                checked.constraint = CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT;
                check_action_diagnose(&checked.action, CHECK_DIAG_INTEGER_REQUIRED, 1);
            }
            else if (mode_ok && check_builtin_misc_constant(table, values + 1, 1, &checked))
            {
                const unsigned __int128 mode = values[1].integer;
                if (mode > 3)
                {
                    checked.constraint = CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
                    check_action_diagnose(&checked.action, CHECK_DIAG_INCOMPATIBLE, 1);
                }
                else
                {
                    output->mode = (uint32_t)mode;
                    known = input->object_size_known[output->mode];
                    const bool source_constant = known && input->object_size_source_constant[output->mode];
                    const bool effects = (values[0].flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
                    if (!source_constant && effects)
                    {
                        // A runtime-only bound cannot bypass the source evaluator's effectful-pointer fallback.
                        known = false;
                    }
                    value = known ? input->object_sizes[output->mode] : output->mode < 2 ? UINT64_MAX : 0;
                    deferred = !source_constant && !effects;
                }
            }
        }
        output->action = checked.action;
        output->warnings = checked.warnings;
        output->warning_operands = checked.warning_operands;
        output->constraint = checked.constraint;
        for (uint32_t i = 0; i < expected; i += 1)
        {
            output->argument_conversions[i] = checked.argument_conversions[i];
            output->action.conversions[i] = checked.argument_conversions[i];
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && constant)
        {
            const uint8_t kind = check_action_kind(table, values[0].type);
            const bool eligible_one = check_action_is_integer(table, values[0].type) || kind == BUSTER_TYPE_KIND_FLOATING ||
                kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR;
            if (input->constant_p_source_proof == CHECK_BUILTIN_QUERY_PROOF_ONE && !eligible_one)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
            }
            else
            {
                check_builtin_query_constant_plan(table, input, values, output);
            }
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->action.value = (CheckValue)
            {
                .type = constant ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_ICE,
                .constant_kind = CHECK_CONSTANT_INTEGER,
                .integer = value,
            };
            output->known = known;
            output->runtime_value = output->action.value;
            if (deferred)
            {
                output->action.value.constant_kind = CHECK_CONSTANT_NONE;
                output->action.value.integer = 0;
                output->action.value.flags = BUSTER_CHECK_ICE_UNEVALUATED;
                output->runtime_value.flags = 0;
            }
        }
    }
}

static __attribute__((unused)) void check_builtin_query(TypeTable* table, const CheckBuiltinQueryAction* actions, CheckBuiltinQueryResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_query_one(table, actions + i, results + i);
    }
}

#endif
