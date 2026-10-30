// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's original baseline-x86 __builtin_flt_rounds leaf. Include after c_builtin_catalog.c and tier0.c. Clang
// 23.1.1 lowers this builtin to llvm.get.rounding, whose x86 implementation observes x87 CW[11:10], not MXCSR.
// RC 00/01/10/11 means nearest/down/up/toward-zero and returns FLT_ROUNDS 1/3/2/0. Our independent realization
// increments RC modulo four, then computes the two-bit Gray code t XOR (t >> 1). Other CW bits are masked away.
// References consulted for behavior only; no implementation was imported:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/llvm/lib/Target/X86/X86ISelLowering.cpp
//
// The source producer may publish these records as one local TEXT atom and call it through existing CALL/fixups.
// It must retain the compiler barrier and floating-environment read: the call cannot be folded, hoisted over an
// environment change, merged with another read or marked pure/readnone. The x87 control word/register stack and
// MXCSR remain unchanged; FNSTCW's x87 condition-code status bits are architecturally undefined.
// The eight-byte stack reservation is restored before return; general-register clobbers are RAX/RCX plus flags.
// U32 result bits map exactly to source INT values 0..3; there are no parameters, branches or symbolic references.
//
// Output owns exactly count instruction records, without readable tail padding. One arena push precedes publication;
// invalid IDs, malformed arenas and exhaustion leave arena bytes/length unchanged. The caller owns arena lifetime.
// Fixed recipe publication is a bounded memcpy; no runtime throughput claim or new typed opcode is implied.
#ifndef BUSTER_C_BUILTIN_ROUNDS_HELPER_INCLUDED
#define BUSTER_C_BUILTIN_ROUNDS_HELPER_INCLUDED

typedef struct CBuiltinRoundsHelper CBuiltinRoundsHelper;
struct CBuiltinRoundsHelper
{
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    bool compiler_barrier;
    bool reads_fp_environment;
};

static const X86Instruction cbuiltin_rounds_helper_records[] =
{
    {
        .immediate = 8,
        .rm = X86_REGISTER_RSP,
        .form = X86_FORM_SUB_RM64_IMM8,
    },
    {
        .rm = X86_REGISTER_RSP,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = X86_FORM_FNSTCW_M16,
    },
    {
        .reg = X86_REGISTER_RAX,
        .rm = X86_REGISTER_RSP,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = X86_FORM_MOVZX_R32_RM16,
    },
    {
        .immediate = 10,
        .rm = X86_REGISTER_RAX,
        .form = X86_FORM_SHR_RM32_IMM8,
    },
    {
        .immediate = 1,
        .rm = X86_REGISTER_RAX,
        .form = X86_FORM_ADD_RM32_IMM8,
    },
    {
        .immediate = 3,
        .rm = X86_REGISTER_RAX,
        .form = X86_FORM_AND_RM32_IMM8,
    },
    {
        .reg = X86_REGISTER_RCX,
        .rm = X86_REGISTER_RAX,
        .form = X86_FORM_MOV_R32_RM32,
    },
    {
        .rm = X86_REGISTER_RCX,
        .form = X86_FORM_SHR_RM32_1,
    },
    {
        .reg = X86_REGISTER_RCX,
        .rm = X86_REGISTER_RAX,
        .form = X86_FORM_XOR_RM32_R32,
    },
    {
        .immediate = 8,
        .rm = X86_REGISTER_RSP,
        .form = X86_FORM_ADD_RM64_IMM8,
    },
    {
        .form = X86_FORM_RET,
    },
};

static CBuiltinRoundsHelper cbuiltin_rounds_helper(CBuiltinId id, Arena* output)
{
    CBuiltinRoundsHelper result = { 0 };
    if (id != CBUILTIN_FLT_ROUNDS)
    {
        result.code.error = TIER0_ERROR_UNSUPPORTED;
    }
    else if (output == NULL || output->length > output->capacity || (output->base == NULL && output->capacity != 0))
    {
        result.code.error = TIER0_ERROR_INVALID;
    }
    else
    {
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, BUSTER_ARRAY_LENGTH(cbuiltin_rounds_helper_records));
        if (records == NULL)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            memcpy(records, cbuiltin_rounds_helper_records, sizeof(cbuiltin_rounds_helper_records));
            result.code.records = records;
            result.code.count = BUSTER_ARRAY_LENGTH(cbuiltin_rounds_helper_records);
            result.code.frame_size = 8;
            result.result_class = BUSTER_TYPED_CLASS_U32;
            result.compiler_barrier = true;
            result.reads_fp_environment = true;
        }
    }
    return result;
}

#endif
