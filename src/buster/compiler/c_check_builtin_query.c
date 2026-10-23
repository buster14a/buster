// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's constant_p and object_size semantic queries. Include after c_check_builtin_misc.c. The producer supplies
// already elaborated operands, immutable source-position tag completions, and explicit AST/object-bound evidence.
// These queries never execute operands or read source memory. Even an input evaluation fault/volatile read does not
// become an effect or fault of the query. Invalid source types, arity and object-size modes still diagnose.
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
// constant_p, source-proven bounds and unknown effectful pointers yield clean source integer constants. Other
// object_size results are deferred by Clang's source evaluator: action.value is nonconstant and permits
// an unevaluated ICE arm, while runtime_value retains the bound or conservative sentinel for lowering. Neither path
// executes query operands. Source argument conversions
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
};

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

static __attribute__((unused)) void check_builtin_query_one(TypeTable* table, const CheckBuiltinQueryAction* input, CheckBuiltinQueryResult* output)
{
    *output = (CheckBuiltinQueryResult){ 0 };
    const bool constant = input->id == CBUILTIN_CONSTANT_P;
    const bool object_size = input->id == CBUILTIN_OBJECT_SIZE;
    const uint32_t expected = constant ? 1 : 2;
    if (!constant && !object_size)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != expected)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
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
        if (checked.action.diagnostic == CHECK_DIAG_NONE && constant)
        {
            value = check_builtin_query_constant(table, input, values);
            known = true;
        }
        else if (checked.action.diagnostic == CHECK_DIAG_NONE)
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
        if (output->action.diagnostic == CHECK_DIAG_NONE)
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
