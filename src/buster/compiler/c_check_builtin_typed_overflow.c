// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's typed __builtin_umull_overflow wrapper. Include after c_check_builtin_atomic.c and c_check_builtin.c.
// Unlike generic mul_overflow, this alias has the ordinary ULONG, ULONG, ULONG* prototype. Apply those conversions
// first, retaining source-position tag history and original conversion records, then reuse the generic U64 action.
// The source adapter evaluates every argument once and performs the separately retained destination store even when
// the boolean/stored bits fold. Runtime expansion uses existing MUL_OVERFLOW with three unsigned-64 descriptors.
//
// Only check_builtin_atomic_argument/convert are reused: those are ordinary source-operand/prototype conversion
// helpers and never perform the atomic object's width, alignment, order or memory checks. Their immutable source
// history and pointer qualifier/signedness diagnostics are useful here independently of atomic operations.
// Qualifier loss into the ordinary ULONG* parameter warns; the converted store itself has unqualified ULONG type.
// No input memory is read and no source store is executed by this checking action.
//
// Valid complex/extended-format conversions without exact payloads, or runtime wide-integer conversions the current
// consumer cannot express, remain typed with explicit unsupported metadata. They do not fabricate a numeric fold.
// Exact integer payloads through 128 bits may be truncated to ULONG before multiplication, as the prototype requires.
// Input/output records are bounded, ordinarily aligned, disjoint from each other and mutable type storage, and need
// no padding. Empty batches permit null slices. Preparation may intern decay types in input order; groups of eight
// feed the existing scalar/vector generic fold. Legal partitions have identical semantics and retain no work.
//
// Audited against llvmorg-23.1.1, without importing implementation: Builtins.td's UmulOverflow UOverflowTemplate has
// no CustomTypeChecking; SemaChecking's BuiltinOverflow applies only to the generic three names; CGBuiltin scalarizes
// the converted operands and stores umul_with_overflow's integer result through the converted pointer.
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/include/clang/Basic/Builtins.td
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp
#ifndef BUSTER_C_CHECK_BUILTIN_TYPED_OVERFLOW_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_TYPED_OVERFLOW_INCLUDED

#define BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_QUALIFIERS BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS
#define BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_POINTER_SIGN BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN
#define BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_FUNCTION_POINTER BUSTER_CHECK_BUILTIN_ATOMIC_WARN_FUNCTION_POINTER
#define BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_CONVERSION 0x01u
#define BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_WIDE_INTEGER 0x02u

typedef struct CheckBuiltinTypedOverflowResult CheckBuiltinTypedOverflowResult;
struct CheckBuiltinTypedOverflowResult
{
    CheckBuiltinResult builtin;
    CheckConversion argument_conversions[3];
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
};

static bool check_builtin_typed_overflow_prepare(TypeTable* table, const CheckBuiltinAction* input, CheckBuiltinTypedOverflowResult* output, CheckBuiltinAction* converted)
{
    *output = (CheckBuiltinTypedOverflowResult)
    {
        .builtin =
        {
            .fold_status = BUSTER_CHECK_BUILTIN_NOT_CONSTANT,
        },
    };
    *converted = (CheckBuiltinAction){ 0 };
    bool accepted = false;
    if (input->id != CBUILTIN_UMULL_OVERFLOW)
    {
        check_action_diagnose(&output->builtin.action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != 3)
    {
        check_action_diagnose(&output->builtin.action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else
    {
        const CheckBuiltinAtomicAction prototype =
        {
            .id = input->id,
            .arguments = input->operands,
            .argument_count = 3,
            .node = input->node,
            .tag_completions = input->tag_completions,
            .tag_completion_count = input->tag_completion_count,
            .source_position = input->source_position,
            .extensions = input->extensions,
        };
        CheckBuiltinAtomicResult checked = { 0 };
        const TypeId pointer = type_pointer(table, BUSTER_TYPE_ULONG);
        if (pointer == 0)
        {
            check_action_diagnose(&checked.action, CHECK_DIAG_RESOURCE, 2);
        }
        for (uint32_t i = 0; i < 3 && checked.action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            CheckValue value = check_builtin_atomic_argument(table, &prototype, &checked, i);
            bool wide = false;
            if (i < 2 && checked.action.diagnostic == CHECK_DIAG_NONE && check_action_is_integer(table, value.type))
            {
                uint32_t width = 0;
                bool signed_type = false;
                const bool represented = check_action_integer_type(table, value.type, &width, &signed_type);
                wide = !represented || (width > 64 && value.constant_kind != CHECK_CONSTANT_INTEGER);
            }
            if (checked.action.diagnostic == CHECK_DIAG_NONE)
            {
                check_builtin_atomic_convert(table, &prototype, &checked, &value, i, i == 2 ? pointer : BUSTER_TYPE_ULONG);
            }
            if (checked.action.diagnostic == CHECK_DIAG_NONE && wide)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_WIDE_INTEGER;
                value.constant_kind = CHECK_CONSTANT_NONE;
                value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
            }
            converted->operands[i] = value;
        }
        output->builtin.action = checked.action;
        output->warnings = checked.warnings;
        output->warning_operands = checked.warning_operands;
        output->unsupported |= (checked.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0 ? BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_CONVERSION : 0;
        for (uint32_t i = 0; i < 3; i += 1)
        {
            output->argument_conversions[i] = checked.argument_conversions[i];
            output->builtin.action.conversions[i] = checked.argument_conversions[i];
        }
        accepted = checked.action.diagnostic == CHECK_DIAG_NONE;
        if (accepted)
        {
            converted->id = CBUILTIN_MUL_OVERFLOW;
            converted->argument_count = 3;
            converted->node = input->node;
            converted->tag_completions = input->tag_completions;
            converted->tag_completion_count = input->tag_completion_count;
            converted->source_position = input->source_position;
            converted->extensions = input->extensions;
            output->evaluated_operands = 7;
            output->runtime_builtin = CBUILTIN_MUL_OVERFLOW;
        }
    }
    if (!accepted)
    {
        output->builtin.fold_status = output->builtin.action.diagnostic == CHECK_DIAG_UNSUPPORTED ? CBUILTIN_UNSUPPORTED : CBUILTIN_INVALID_TYPE;
    }
    return accepted;
}

static __attribute__((unused)) void check_builtin_typed_overflow(TypeTable* table, const CheckBuiltinAction* actions, CheckBuiltinTypedOverflowResult* results, uint32_t count, bool scalar)
{
    (void)scalar;

    for (uint32_t first = 0; first < count;)
    {
        const uint32_t lanes = count - first < BUSTER_CBUILTIN_LANES ? count - first : BUSTER_CBUILTIN_LANES;
        CheckBuiltinAction converted[BUSTER_CBUILTIN_LANES] = { 0 };
        CheckBuiltinResult folded[BUSTER_CBUILTIN_LANES] = { 0 };
        bool accepted[BUSTER_CBUILTIN_LANES] = { 0 };
        bool any = false;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            accepted[lane] = check_builtin_typed_overflow_prepare(table, actions + first + lane, results + first + lane, converted + lane);
            any |= accepted[lane];
        }
        if (any)
        {
            check_builtins(table, converted, folded, lanes, BUSTER_SCALAR_PATH(scalar));
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                if (accepted[lane])
                {
                    CheckBuiltinTypedOverflowResult* output = results + first + lane;
                    output->builtin = folded[lane];
                    for (uint32_t i = 0; i < 3; i += 1)
                    {
                        output->builtin.action.conversions[i] = output->argument_conversions[i];
                    }
                    if (output->builtin.action.diagnostic != CHECK_DIAG_NONE)
                    {
                        output->evaluated_operands = 0;
                        output->runtime_builtin = CBUILTIN_NONE;
                    }
                }
            }
        }
        first += lanes;
    }
}

static __attribute__((unused)) void check_builtin_typed_overflow_one(TypeTable* table, const CheckBuiltinAction* action, CheckBuiltinTypedOverflowResult* result, bool scalar)
{
    (void)scalar;

    check_builtin_typed_overflow(table, action, result, 1, BUSTER_SCALAR_PATH(scalar));
}

#endif
