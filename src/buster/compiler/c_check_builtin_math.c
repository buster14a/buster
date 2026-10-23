// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's confirmed F32/F64 math slice. Include after c_check_builtin_float.c. The producer supplies bounded source
// values and immutable tag-completion history. Ordinary prototype conversions precede each operation; all operands
// evaluate exactly once. Converted arguments are retained for the runtime producer, including actual FP classes.
// Complex/wider conversions stay valid source with an explicit unsupported requirement, never a host approximation.
// No input pointer, host errno or host floating environment is read or modified intentionally by this module.
//
// Exact Clang 23.1.1 ExprConstant.cpp folds copysign/f but has no AST folding cases for the other twelve names.
// Thus only copysign can produce a source floating constant here. An optimizer's later intrinsic folding cannot make
// sqrt/ceil/floor/trunc/nearbyint/rint valid source constant initializers. No result is an ICE or floating literal.
// Copysign copies representation sign only, preserving NaN payload/quiet state. Prototype conversions remain a
// separate producer obligation and can themselves depend on the floating environment, as ordinary C conversions do.
// The default action has ordinary nearest/default source options. nondefault_fp_environment marks unrepresented
// rounding/exception settings; then all nonidentity conversions, including exact widenings and source constant
// contexts, stay runtime requirements until numerical mode metadata exists. Identity-format copysign needs neither
// rounding nor arithmetic and remains foldable under those settings. Conversion environment effects are retained.
//
// Runtime metadata conservatively retains errno for sqrt and floating-environment reads/possible exceptions for
// every family except copysign. reads_fp_environment includes DAZ/exception-mask dependence, not only numerical
// rounding mode. nearbyint suppresses its operation's FE_INEXACT; rint can raise it. The source/runtime
// metadata for ceil/floor/trunc conservatively permits inexact, which C allows but does not require for those calls.
// producer must preserve these effects and choose a helper satisfying errno/environment requirements. runtime_builtin
// names the remaining operation, not an implementation claim. This action neither grants a catalog capability nor
// rejects valid runtime source because a helper is unavailable. This module emits no machine code or library call.
//
// Actions/results, argument slices and mutable type storage must be disjoint, naturally aligned and writable/readable
// only to their logical bounds. No padding is required. Zero-count batches accept null slices. Batches preserve source
// order because decay types may be interned; legal partitions produce the same semantic results. Rules, not code, from:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/include/clang/Basic/Builtins.td
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/AST/ExprConstant.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp
#ifndef BUSTER_C_CHECK_BUILTIN_MATH_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_MATH_INCLUDED

#define BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_CONVERSION 0x01u
#define BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_FP_ENVIRONMENT 0x02u

typedef enum CheckBuiltinMathKind
{
    CHECK_BUILTIN_MATH_NONE,
    CHECK_BUILTIN_MATH_SQRT,
    CHECK_BUILTIN_MATH_COPYSIGN,
    CHECK_BUILTIN_MATH_CEIL,
    CHECK_BUILTIN_MATH_FLOOR,
    CHECK_BUILTIN_MATH_TRUNC,
    CHECK_BUILTIN_MATH_NEARBYINT,
    CHECK_BUILTIN_MATH_RINT,
} CheckBuiltinMathKind;

typedef struct CheckBuiltinMathAction CheckBuiltinMathAction;
struct CheckBuiltinMathAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    bool nondefault_fp_environment;
};

typedef struct CheckBuiltinMathResult CheckBuiltinMathResult;
struct CheckBuiltinMathResult
{
    CheckActionResult action;
    CheckConversion argument_conversions[2];
    CheckValue converted_arguments[2];
    TypeId floating_type;
    uint32_t evaluated_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    bool folded;
    bool compiler_barrier;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool may_raise_inexact;
    bool may_set_errno;
};

static CheckBuiltinMathKind check_builtin_math_kind(CBuiltinId id)
{
    CheckBuiltinMathKind result = CHECK_BUILTIN_MATH_NONE;
    switch (id)
    {
    case CBUILTIN_SQRT:
    case CBUILTIN_SQRTF: result = CHECK_BUILTIN_MATH_SQRT; break;
    case CBUILTIN_COPYSIGN:
    case CBUILTIN_COPYSIGNF: result = CHECK_BUILTIN_MATH_COPYSIGN; break;
    case CBUILTIN_CEIL:
    case CBUILTIN_CEILF: result = CHECK_BUILTIN_MATH_CEIL; break;
    case CBUILTIN_FLOOR:
    case CBUILTIN_FLOORF: result = CHECK_BUILTIN_MATH_FLOOR; break;
    case CBUILTIN_TRUNC:
    case CBUILTIN_TRUNCF: result = CHECK_BUILTIN_MATH_TRUNC; break;
    case CBUILTIN_NEARBYINT:
    case CBUILTIN_NEARBYINTF: result = CHECK_BUILTIN_MATH_NEARBYINT; break;
    case CBUILTIN_RINT:
    case CBUILTIN_RINTF: result = CHECK_BUILTIN_MATH_RINT; break;
    default: break;
    }
    return result;
}

static TypeId check_builtin_math_parameter(CBuiltinId id)
{
    const bool narrow = id == CBUILTIN_SQRTF || id == CBUILTIN_COPYSIGNF || id == CBUILTIN_CEILF || id == CBUILTIN_FLOORF ||
                        id == CBUILTIN_TRUNCF || id == CBUILTIN_NEARBYINTF || id == CBUILTIN_RINTF;
    const TypeId result = check_builtin_math_kind(id) == CHECK_BUILTIN_MATH_NONE ? 0 : narrow ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
    return result;
}

static void check_builtin_math_copy_sign(const CheckValue* magnitude, const CheckValue* sign, CheckValue* result)
{
    // CheckValue uses host x87 extended precision for supported source formats. Work on its representation so
    // even a signaling NaN is not passed through host floating arithmetic merely to inspect or change its sign.
    _Static_assert(sizeof(long double) == 16 && LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384, "the checker host uses x87 extended precision");
    uint8_t value_bytes[16];
    uint8_t sign_bytes[16];
    memcpy(value_bytes, &magnitude->floating, sizeof(value_bytes));
    memcpy(sign_bytes, &sign->floating, sizeof(sign_bytes));
    value_bytes[9] = (value_bytes[9] & 0x7f) | (sign_bytes[9] & 0x80);
    memcpy(&result->floating, value_bytes, sizeof(value_bytes));
    result->constant_kind = CHECK_CONSTANT_FLOATING;
}

static void check_builtin_math_convert(TypeTable* table, const CheckBuiltinMathAction* input, CheckBuiltinMathResult* output, uint32_t index)
{
    CheckValue* value = output->converted_arguments + index;
    const TypeId parameter = output->floating_type;
    if (!check_action_is_arithmetic(table, value->type))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARITHMETIC_REQUIRED, index);
    }
    else
    {
        const uint8_t kind = check_action_kind(table, value->type);
        uint32_t width = 0;
        bool is_signed = false;
        const bool wide_integer = check_action_is_integer(table, value->type) && !check_action_integer_type(table, value->type, &width, &is_signed);
        if (kind == BUSTER_TYPE_KIND_COMPLEX || (kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, value->type)) || wide_integer)
        {
            output->unsupported |= BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_CONVERSION;
            value->constant_kind = CHECK_CONSTANT_NONE;
        }
        if (type_unqualified(type_strip_aligned(table, value->type)) == parameter)
        {
            // The already represented source format requires no numeric round-trip or NaN quieting.
            output->argument_conversions[index].type = parameter;
            output->argument_conversions[index].flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT;
            value->type = parameter;
        }
        else
        {
            if (input->nondefault_fp_environment)
            {
                // This context witness does not identify a directed numerical rounding mode. Even an otherwise
                // known source value must not be converted by the host and published as that unknown-mode result.
                output->unsupported |= BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_FP_ENVIRONMENT;
                value->constant_kind = CHECK_CONSTANT_NONE;
                value->flags |= BUSTER_CHECK_SIDE_EFFECT;
                output->reads_fp_environment = true;
                output->may_raise_fp_exceptions = true;
                output->may_raise_inexact = true;
                output->compiler_barrier = true;
            }
            check_action_conversion(table, value, parameter, BUSTER_CHECK_CONVERT_ASSIGNMENT, output->argument_conversions + index, input->node);
        }
    }
}

static __attribute__((unused)) void check_builtin_math_one(TypeTable* table, const CheckBuiltinMathAction* input, CheckBuiltinMathResult* output)
{
    *output = (CheckBuiltinMathResult){ 0 };
    const CheckBuiltinMathKind kind = check_builtin_math_kind(input->id);
    const uint32_t count = kind == CHECK_BUILTIN_MATH_COPYSIGN ? 2 : 1;
    if (kind == CHECK_BUILTIN_MATH_NONE)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != count)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        output->floating_type = check_builtin_math_parameter(input->id);
        output->action.value.type = output->floating_type;
        output->action.value.category = CHECK_RVALUE;
        output->evaluated_operands = (UINT32_C(1) << count) - 1;
        output->runtime_builtin = input->id;
        const CheckBuiltinFloatAction arguments =
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
        CheckBuiltinFloatResult checked = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            output->converted_arguments[i] = check_builtin_float_argument(table, &arguments, &checked, i);
            output->argument_conversions[i] = checked.argument_conversions[i];
            if (checked.action.diagnostic != CHECK_DIAG_NONE)
            {
                check_action_diagnose(&output->action, checked.action.diagnostic, checked.action.diagnostic_operand);
            }
            if (output->action.diagnostic == CHECK_DIAG_NONE)
            {
                check_builtin_math_convert(table, input, output, i);
            }
            check_action_inherit(&output->action.value, output->converted_arguments + i);
            output->action.conversions[i] = output->argument_conversions[i];
        }
        if (kind == CHECK_BUILTIN_MATH_COPYSIGN && output->action.diagnostic == CHECK_DIAG_NONE && output->action.value.fault == CHECK_FAULT_NONE &&
            output->converted_arguments[0].constant_kind == CHECK_CONSTANT_FLOATING && output->converted_arguments[1].constant_kind == CHECK_CONSTANT_FLOATING)
        {
            check_builtin_math_copy_sign(output->converted_arguments, output->converted_arguments + 1, &output->action.value);
            output->folded = (output->action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
            if (output->folded)
            {
                output->runtime_builtin = CBUILTIN_NONE;
            }
            else
            {
                // Existing source data writers consume constant_kind. Do not let a merely known mathematical
                // value make an effectful call appear to be a valid source constant initializer.
                output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            }
        }
        if (kind != CHECK_BUILTIN_MATH_COPYSIGN && output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->may_set_errno = kind == CHECK_BUILTIN_MATH_SQRT;
            output->reads_fp_environment = true;
            output->may_raise_fp_exceptions = true;
            output->may_raise_inexact |= kind != CHECK_BUILTIN_MATH_NEARBYINT;
            output->compiler_barrier = true;
            output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
        }
    }
    // Floating calls never become integer-constant or literal provenance, even after an exact source fold.
    output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL);
    if (output->action.diagnostic != CHECK_DIAG_NONE)
    {
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        output->folded = false;
    }
}

static __attribute__((unused)) void check_builtin_math(TypeTable* table, const CheckBuiltinMathAction* inputs, CheckBuiltinMathResult* outputs, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_math_one(table, inputs + i, outputs + i);
    }
}

#endif
