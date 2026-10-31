// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's original binary32/binary64 SysV math leaves. Include after the builtin catalog, lane.c and tier0.c.
// The caller evaluates/converts source operands once, then passes their raw bits in U64 integer registers. Float
// inputs use the low 32 bits and float results clear the high 32 bits. This is a bit transport ABI, never a numeric
// integer conversion. Copysign uses RDI/RSI; sqrt uses RDI plus a real writable int32 errno address in RSI; other
// functions use RDI alone. RAX returns bits. No leaf calls a math symbol, errno accessor or other external function.
// The errno-enabled sqrt contract targets this compiler's Linux x86-64 SysV environment: EDOM comes from errno.h,
// and the source producer supplies the current thread's actual errno address, not a guessed TLS/frame location.
//
// Copysign is integer sign selection and preserves signaling NaNs without raising exceptions. Other NaNs retain
// their sign/payload; signaling NaNs are quieted by one scalar ADD under the original MXCSR. Finite rounding uses
// bounded IEEE exponent/significand arithmetic. Nearbyint reads RC; ceil/floor/trunc select fixed directions. DAZ
// converts subnormal inputs to signed zero, matching the architectural rounding operation. No rounding leaf writes
// MXCSR or x87 state. Existing status flags survive. Nearbyint never raises precision, including when unmasked.
// Rint additionally executes a discarded, normal inexact ADD for fractional effective inputs: 2^23+0.25f or
// 2^52+0.25. Its exact sum is unrepresentable under every RC, so it raises/traps precision without denormal/underflow
// or a temporary control change. Infinities, exact integers and signed zero pass through without arithmetic.
//
// Sqrt's ordered comparison with zero naturally honors DAZ and excludes NaNs. Ordered negative input stores EDOM
// before SQRTSS/SD; -0 and DAZ-converted negative subnormals leave errno alone. The hardware operation supplies
// current rounding, signed zero, NaN quieting and exceptions. The x87 control word/register stack remain untouched.
// General clobbers are caller-saved RAX/RCX/RDX/R8/R9/R10/R11, flags and XMM0/XMM1. A 16-byte stack reservation is
// restored before every normal return. No assumptions about SSE4.1, AVX or the compiler host's ISA are made.
//
// Behavioral references only; no implementation was imported:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Basic/Builtins.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/llvm/docs/LangRef.md
// https://cdrdv2.intel.com/v1/dl/getContent/671241 (ROUNDSS/ROUNDSD, SQRTSS/SQRTSD and ADDSS/ADDSD)
// https://cdrdv2.intel.com/v1/dl/getContent/671436 (MXCSR and NaN propagation)
//
// Records/branch columns are staged within fixed bounds, then published with one aligned arena push. Invalid IDs,
// malformed arenas and capacity errors leave arena length/bytes unchanged. Branches use Tier0Code's ordinary rel8
// record-index contract and require existing branch sizing before encoding. Output owns exact logical slices and
// needs no readable tail padding. All calls retain compiler_barrier and FP/errno metadata: no pure/readnone claims,
// hoisting across environment changes, or removal of a call whose result is unused. These bodies alone establish
// no source dispatcher, raw-bit adapter, Tier1 acceptance, extended-format support or target-host test result.
#ifndef BUSTER_C_BUILTIN_MATH_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_MATH_HELPERS_INCLUDED

#include <errno.h>

#define BUSTER_CBUILTIN_MATH_RECORDS 256u
#define BUSTER_CBUILTIN_MATH_BRANCHES 48u

typedef struct CBuiltinMathHelper CBuiltinMathHelper;
struct CBuiltinMathHelper
{
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    uint8_t parameter_classes[2];
    bool compiler_barrier;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool may_raise_inexact;
    bool requires_errno_address;
};

typedef enum CBuiltinMathLabel
{
    CBUILTIN_MATH_RAW,
    CBUILTIN_MATH_ZERO,
    CBUILTIN_MATH_NAN,
    CBUILTIN_MATH_ACTIVE,
    CBUILTIN_MATH_SMALL,
    CBUILTIN_MATH_DECIDE,
    CBUILTIN_MATH_DOWN,
    CBUILTIN_MATH_NEAREST,
    CBUILTIN_MATH_INCREMENT,
    CBUILTIN_MATH_FINISH,
    CBUILTIN_MATH_SQRT,
    CBUILTIN_MATH_END,
    CBUILTIN_MATH_LABEL_COUNT,
} CBuiltinMathLabel;

typedef struct CBuiltinMathRecipe CBuiltinMathRecipe;
struct CBuiltinMathRecipe
{
    X86Instruction records[BUSTER_CBUILTIN_MATH_RECORDS];
    uint32_t branches[BUSTER_CBUILTIN_MATH_BRANCHES];
    uint32_t labels[BUSTER_CBUILTIN_MATH_BRANCHES];
    uint32_t positions[CBUILTIN_MATH_LABEL_COUNT];
    uint32_t count;
    uint32_t branch_count;
    bool full;
};

static void cbuiltin_math_append(CBuiltinMathRecipe* recipe, X86Instruction record)
{
    if (recipe->count < BUSTER_CBUILTIN_MATH_RECORDS)
    {
        recipe->records[recipe->count] = record;
        recipe->count += 1;
    }
    else
    {
        recipe->full = true;
    }
}

static void cbuiltin_math_rr(CBuiltinMathRecipe* recipe, uint16_t form, uint8_t destination, uint8_t source)
{
    cbuiltin_math_append(recipe, (X86Instruction){ .form = form, .reg = destination, .rm = source });
}

static void cbuiltin_math_immediate(CBuiltinMathRecipe* recipe, uint16_t form, uint8_t operand, int32_t immediate)
{
    cbuiltin_math_append(recipe, (X86Instruction){ .form = form, .rm = operand, .immediate = immediate });
}

static void cbuiltin_math_bits(CBuiltinMathRecipe* recipe, uint8_t destination, uint64_t bits)
{
    cbuiltin_math_append(recipe, (X86Instruction)
    {
        .form = X86_FORM_MOV_R64_IMM64,
        .rm = destination,
        .immediate = (int32_t)bits,
        .displacement = (int32_t)(bits >> 32),
    });
}

static void cbuiltin_math_stack(CBuiltinMathRecipe* recipe, uint16_t form, uint8_t reg, int32_t displacement)
{
    cbuiltin_math_append(recipe, (X86Instruction){ .form = form, .reg = reg, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = displacement });
}

static void cbuiltin_math_jump(CBuiltinMathRecipe* recipe, uint16_t form, CBuiltinMathLabel label)
{
    if (recipe->branch_count < BUSTER_CBUILTIN_MATH_BRANCHES)
    {
        recipe->branches[recipe->branch_count] = recipe->count;
        recipe->labels[recipe->branch_count] = label;
        recipe->branch_count += 1;
        cbuiltin_math_append(recipe, (X86Instruction){ .form = form });
    }
    else
    {
        recipe->full = true;
    }
}

static void cbuiltin_math_label(CBuiltinMathRecipe* recipe, CBuiltinMathLabel label)
{
    recipe->positions[label] = recipe->count;
}

static void cbuiltin_math_copy_sign(CBuiltinMathRecipe* recipe, bool single)
{
    const uint64_t sign = single ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000);
    cbuiltin_math_bits(recipe, X86_REGISTER_RCX, sign);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RSI);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RCX);
    cbuiltin_math_immediate(recipe, X86_FORM_NOT_RM64, X86_REGISTER_RCX, 0);
    cbuiltin_math_rr(recipe, single ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX);
    cbuiltin_math_rr(recipe, X86_FORM_OR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDX);
    cbuiltin_math_append(recipe, (X86Instruction){ .form = X86_FORM_RET });
}

static void cbuiltin_math_sqrt(CBuiltinMathRecipe* recipe, bool single)
{
    cbuiltin_math_immediate(recipe, X86_FORM_SUB_RM64_IMM8, X86_REGISTER_RSP, 16);
    cbuiltin_math_stack(recipe, X86_FORM_MOV_RM64_R64, X86_REGISTER_RDI, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, 0, 0);
    cbuiltin_math_rr(recipe, X86_FORM_PXOR_X_XM128, 1, 1);
    cbuiltin_math_rr(recipe, single ? X86_FORM_UCOMISS_X_XM32 : X86_FORM_UCOMISD_X_XM64, 0, 1);
    cbuiltin_math_jump(recipe, X86_FORM_JP_REL8, CBUILTIN_MATH_SQRT);
    cbuiltin_math_jump(recipe, X86_FORM_JAE_REL8, CBUILTIN_MATH_SQRT);
    cbuiltin_math_append(recipe, (X86Instruction){ .form = X86_FORM_MOV_RM32_IMM32, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .immediate = EDOM });
    cbuiltin_math_label(recipe, CBUILTIN_MATH_SQRT);
    cbuiltin_math_rr(recipe, single ? X86_FORM_SQRTSS_X_XM32 : X86_FORM_SQRTSD_X_XM64, 0, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X, 0, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, 0);
    cbuiltin_math_immediate(recipe, X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RSP, 16);
    cbuiltin_math_append(recipe, (X86Instruction){ .form = X86_FORM_RET });
}

static void cbuiltin_math_round(CBuiltinMathRecipe* recipe, uint32_t operation, bool single)
{
    const uint32_t precision = single ? 23 : 52;
    const uint32_t bias = single ? 127 : 1023;
    const uint64_t sign = single ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000);
    const uint64_t infinity = single ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t one = single ? UINT64_C(0x3f800000) : UINT64_C(0x3ff0000000000000);
    const uint64_t half = single ? UINT64_C(0x3f000000) : UINT64_C(0x3fe0000000000000);
    cbuiltin_math_immediate(recipe, X86_FORM_SUB_RM64_IMM8, X86_REGISTER_RSP, 16);
    cbuiltin_math_bits(recipe, X86_REGISTER_R9, sign - 1);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R9);
    cbuiltin_math_bits(recipe, X86_REGISTER_R9, infinity);
    cbuiltin_math_rr(recipe, X86_FORM_CMP_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R9);
    cbuiltin_math_jump(recipe, X86_FORM_JA_REL8, CBUILTIN_MATH_NAN);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_RAW);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RAX);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_RAW);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RAX);
    cbuiltin_math_immediate(recipe, X86_FORM_SHR_RM64_IMM8, X86_REGISTER_RCX, (int32_t)precision);
    cbuiltin_math_immediate(recipe, X86_FORM_CMP_RM32_IMM32, X86_REGISTER_RCX, (int32_t)(bias + precision));
    cbuiltin_math_jump(recipe, X86_FORM_JAE_REL8, CBUILTIN_MATH_RAW);
    cbuiltin_math_bits(recipe, X86_REGISTER_R9, sign);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_RDI);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R9);
    cbuiltin_math_stack(recipe, X86_FORM_STMXCSR_M32, 0, 8);
    cbuiltin_math_stack(recipe, X86_FORM_MOV_R32_RM32, X86_REGISTER_R8, 8);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM32_R32, X86_REGISTER_RCX, X86_REGISTER_RCX);
    cbuiltin_math_jump(recipe, X86_FORM_JNE_REL8, CBUILTIN_MATH_ACTIVE);
    cbuiltin_math_immediate(recipe, X86_FORM_TEST_RM32_IMM32, X86_REGISTER_R8, 0x40);
    cbuiltin_math_jump(recipe, X86_FORM_JNE_REL8, CBUILTIN_MATH_ZERO);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_ACTIVE);
    if (operation == 5 || operation == 6)
    {
        cbuiltin_math_immediate(recipe, X86_FORM_SHR_RM32_IMM8, X86_REGISTER_R8, 13);
        cbuiltin_math_immediate(recipe, X86_FORM_AND_RM32_IMM8, X86_REGISTER_R8, 3);
    }
    else
    {
        cbuiltin_math_immediate(recipe, X86_FORM_MOV_R32_IMM32, X86_REGISTER_R8, operation == 2 ? 2 : operation == 3 ? 1 : 3);
    }
    cbuiltin_math_immediate(recipe, X86_FORM_CMP_RM32_IMM32, X86_REGISTER_RCX, (int32_t)bias);
    cbuiltin_math_jump(recipe, X86_FORM_JB_REL8, CBUILTIN_MATH_SMALL);
    cbuiltin_math_immediate(recipe, X86_FORM_MOV_R32_IMM32, X86_REGISTER_RDX, (int32_t)(bias + precision));
    cbuiltin_math_rr(recipe, X86_FORM_SUB_R32_RM32, X86_REGISTER_RDX, X86_REGISTER_RCX);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R32_RM32, X86_REGISTER_RCX, X86_REGISTER_RDX);
    cbuiltin_math_immediate(recipe, X86_FORM_MOV_R32_IMM32, X86_REGISTER_R9, 1);
    cbuiltin_math_immediate(recipe, X86_FORM_SHL_RM64_CL, X86_REGISTER_R9, 0);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_R9);
    cbuiltin_math_immediate(recipe, X86_FORM_SUB_RM64_IMM8, X86_REGISTER_R11, 1);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_R11);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM64_R64, X86_REGISTER_RDX, X86_REGISTER_RDX);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_RAW);
    cbuiltin_math_immediate(recipe, X86_FORM_NOT_RM64, X86_REGISTER_R11, 0);
    cbuiltin_math_rr(recipe, X86_FORM_AND_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R11);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_R9);
    cbuiltin_math_immediate(recipe, X86_FORM_SHR_RM64_1, X86_REGISTER_R11, 0);
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_DECIDE);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_SMALL);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
    cbuiltin_math_bits(recipe, X86_REGISTER_R9, one);
    cbuiltin_math_bits(recipe, X86_REGISTER_R11, half);
    cbuiltin_math_immediate(recipe, X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 0);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_DECIDE);
    cbuiltin_math_immediate(recipe, X86_FORM_CMP_RM32_IMM8, X86_REGISTER_R8, 3);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_FINISH);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM32_R32, X86_REGISTER_R8, X86_REGISTER_R8);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_NEAREST);
    cbuiltin_math_immediate(recipe, X86_FORM_CMP_RM32_IMM8, X86_REGISTER_R8, 1);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_DOWN);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R10);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_INCREMENT);
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_FINISH);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_DOWN);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R10);
    cbuiltin_math_jump(recipe, X86_FORM_JNE_REL8, CBUILTIN_MATH_INCREMENT);
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_FINISH);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_NEAREST);
    cbuiltin_math_rr(recipe, X86_FORM_CMP_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_R11);
    cbuiltin_math_jump(recipe, X86_FORM_JA_REL8, CBUILTIN_MATH_INCREMENT);
    cbuiltin_math_jump(recipe, X86_FORM_JB_REL8, CBUILTIN_MATH_FINISH);
    cbuiltin_math_rr(recipe, X86_FORM_TEST_RM64_R64, X86_REGISTER_R9, X86_REGISTER_RAX);
    cbuiltin_math_jump(recipe, X86_FORM_JE_REL8, CBUILTIN_MATH_FINISH);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_INCREMENT);
    cbuiltin_math_rr(recipe, X86_FORM_ADD_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R9);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_FINISH);
    cbuiltin_math_rr(recipe, X86_FORM_OR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10);
    if (operation == 6)
    {
        cbuiltin_math_bits(recipe, X86_REGISTER_R11, single ? UINT64_C(0x4b000000) : UINT64_C(0x4330000000000000));
        cbuiltin_math_stack(recipe, X86_FORM_MOV_RM64_R64, X86_REGISTER_R11, 0);
        cbuiltin_math_stack(recipe, single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, 0, 0);
        cbuiltin_math_bits(recipe, X86_REGISTER_R11, single ? UINT64_C(0x3e800000) : UINT64_C(0x3fd0000000000000));
        cbuiltin_math_stack(recipe, X86_FORM_MOV_RM64_R64, X86_REGISTER_R11, 0);
        cbuiltin_math_stack(recipe, single ? X86_FORM_ADDSS_X_XM32 : X86_FORM_ADDSD_X_XM64, 0, 0);
    }
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_END);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_ZERO);
    cbuiltin_math_rr(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10);
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_END);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_NAN);
    cbuiltin_math_stack(recipe, X86_FORM_MOV_RM64_R64, X86_REGISTER_RDI, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, 0, 0);
    cbuiltin_math_rr(recipe, single ? X86_FORM_ADDSS_X_XM32 : X86_FORM_ADDSD_X_XM64, 0, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X, 0, 0);
    cbuiltin_math_stack(recipe, single ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, 0);
    cbuiltin_math_jump(recipe, X86_FORM_JMP_REL8, CBUILTIN_MATH_END);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_RAW);
    cbuiltin_math_rr(recipe, single ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI);
    cbuiltin_math_label(recipe, CBUILTIN_MATH_END);
    cbuiltin_math_immediate(recipe, X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RSP, 16);
    cbuiltin_math_append(recipe, (X86Instruction){ .form = X86_FORM_RET });
}

static __attribute__((unused)) CBuiltinMathHelper cbuiltin_math_helper(CBuiltinId id, Arena* output)
{
    CBuiltinMathHelper result = { 0 };
    result.code.error = TIER0_ERROR_UNSUPPORTED;
    const uint32_t index = (uint32_t)id - (uint32_t)CBUILTIN_SQRT;
    if (index < 14)
    {
        if (output == NULL || output->length > output->capacity || (output->base == NULL && output->capacity != 0))
        {
            result.code.error = TIER0_ERROR_INVALID;
        }
        else
        {
            const uint32_t operation = index >> 1;
            const bool single = (index & 1) != 0;
            CBuiltinMathRecipe recipe = { 0 };
            if (operation == 0)
            {
                cbuiltin_math_sqrt(&recipe, single);
            }
            else if (operation == 1)
            {
                cbuiltin_math_copy_sign(&recipe, single);
            }
            else
            {
                cbuiltin_math_round(&recipe, operation, single);
            }
            if (recipe.full)
            {
                result.code.error = TIER0_ERROR_MEMORY;
            }
            else
            {
                const uint64_t record_bytes = (uint64_t)recipe.count * sizeof(X86Instruction);
                const uint64_t branch_bytes = (uint64_t)recipe.branch_count * sizeof(uint32_t);
                uint8_t* storage = arena_push_array(output, record_bytes + 2 * branch_bytes, 1, _Alignof(X86Instruction));
                if (storage == NULL)
                {
                    result.code.error = TIER0_ERROR_MEMORY;
                }
                else
                {
                    result.code.records = (X86Instruction*)storage;
                    result.code.count = recipe.count;
                    // Sqrt and rounding reserve 16 bytes until their common epilogue; copysign has no frame.
                    result.code.frame_size = operation == 1 ? 0 : 16;
                    memcpy(result.code.records, recipe.records, (size_t)record_bytes);
                    result.code.branches = (uint32_t*)(storage + record_bytes);
                    result.code.targets = (uint32_t*)(storage + record_bytes + branch_bytes);
                    result.code.branch_count = recipe.branch_count;
                    for (uint32_t i = 0; i < recipe.branch_count; i += 1)
                    {
                        result.code.branches[i] = recipe.branches[i];
                        result.code.targets[i] = recipe.positions[recipe.labels[i]];
                    }
                    result.code.error = TIER0_OK;
                    result.parameter_count = operation <= 1 ? 2 : 1;
                    result.result_class = BUSTER_TYPED_CLASS_U64;
                    result.parameter_classes[0] = BUSTER_TYPED_CLASS_U64;
                    result.parameter_classes[1] = operation <= 1 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
                    result.compiler_barrier = true;
                    result.reads_fp_environment = operation != 1;
                    result.may_raise_fp_exceptions = operation != 1;
                    result.may_raise_inexact = operation == 0 || operation == 6;
                    result.requires_errno_address = operation == 0;
                }
            }
        }
    }
    return result;
}

#endif
