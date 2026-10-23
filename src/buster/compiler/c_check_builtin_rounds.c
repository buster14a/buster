// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's zero-argument floating-environment query. Include after c_check_actions.c and c_builtin_catalog.c.
// The result is an ordinary runtime int, never an ICE or a speculative constant. It reads the current floating
// environment; SIDE_EFFECT and compiler_barrier conservatively preserve its order around environment changes.
// No source operand is evaluated or inspected, even when an invalid arity carries inaccessible argument storage.
// The x86 helper owns the target-specific realization. This action neither reads the host environment nor grants
// source/backend/catalog support. Clang 23.1.1's __builtin_flt_rounds behavior supplied rules, not implementation.
// Reference: https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp
// Actions/results must be disjoint, naturally aligned bounded slices; no padding is required. Zero count permits
// null slices. Each fixed-size action is independent; the bounded batch performs no allocation or type interning.
#ifndef BUSTER_C_CHECK_BUILTIN_ROUNDS_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_ROUNDS_INCLUDED

typedef struct CheckBuiltinRoundsAction CheckBuiltinRoundsAction;
struct CheckBuiltinRoundsAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
};

typedef struct CheckBuiltinRoundsResult CheckBuiltinRoundsResult;
struct CheckBuiltinRoundsResult
{
    CheckActionResult action;
    uint32_t evaluated_operands;
    CBuiltinId runtime_builtin;
    bool compiler_barrier;
    bool reads_fp_environment;
};

static __attribute__((unused)) void check_builtin_rounds_one(const CheckBuiltinRoundsAction* input, CheckBuiltinRoundsResult* output)
{
    *output = (CheckBuiltinRoundsResult){ 0 };
    if (input->id != CBUILTIN_FLT_ROUNDS)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else
    {
        output->action.value.type = BUSTER_TYPE_INT;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        output->action.value.flags = BUSTER_CHECK_SIDE_EFFECT;
        output->runtime_builtin = CBUILTIN_FLT_ROUNDS;
        output->compiler_barrier = true;
        output->reads_fp_environment = true;
    }
}

static __attribute__((unused)) void check_builtin_rounds(const CheckBuiltinRoundsAction* inputs, CheckBuiltinRoundsResult* outputs, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_rounds_one(inputs + i, outputs + i);
    }
}

#endif
