// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The AMX value builtins' leaves. Their private ABI takes the builtin's scalar arguments unchanged, tile arguments
// as addresses, and a final address for a result. A tile value's defined bytes are its packed active rows, with
// colsb bytes per row, as Clang 23's x86_amx/vector conversions emit. The remainder of a result's 1024-byte object
// is unspecified there; these leaves initialize it to zero so that no uninitialized frame bytes escape.
//
// Every leaf owns its 64-byte-aligned tilecfg, initialized completely to palette 1, and releases the caller-clobbered
// tile state before returning. It neither reads nor preserves an incoming tile configuration. Dynamic dimensions
// retain the hardware's tilecfg and instruction checks. No AMX instruction executes while compiling these leaves.
#ifndef BUSTER_C_BUILTIN_AMX_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_AMX_HELPERS_INCLUDED

static CBuiltinX86Helper cbuiltin_amx_helper(const CBuiltinX86HelperInfo* info, Arena* output)
{
    CBuiltinX86Helper result = { 0 };
    X86Instruction records[64];
    uint32_t count = 0;
    const bool dot = info->amx == CBUILTIN_AMX_DOT;
    const bool store = info->amx == CBUILTIN_AMX_STORE;
    const bool row = info->amx == CBUILTIN_AMX_ROW;
    const uint8_t destination = dot ? X86_REGISTER_R11 : info->amx == CBUILTIN_AMX_ZERO ? X86_REGISTER_RDX : X86_REGISTER_R8;
    records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_RBP };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP };
    records[count++] = (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = 64 };
    records[count++] = (X86Instruction){ .form = X86_FORM_AND_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = -64 };
    records[count++] = (X86Instruction){ .form = X86_FORM_XOR_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX };
    for (uint32_t offset = 0; offset < 64; offset += 8)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)offset };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM8_IMM8, .rm = X86_REGISTER_RSP,
                                        .mode = BUSTER_X86_MEMORY_BASE, .immediate = 1 };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM16_R16, .reg = X86_REGISTER_RSI, .rm = X86_REGISTER_RSP,
                                        .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16 };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM8_R8, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP,
                                        .mode = BUSTER_X86_MEMORY_BASE, .displacement = 48 };
    if (dot)
    {
        // dst m x n, src1 m x k, src2 k/4 x n, with the multiply's dword grouping for every AMX dot family.
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM16_R16, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RSP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = 18 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM8_R8, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = 49 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM16_R16, .reg = X86_REGISTER_RSI, .rm = X86_REGISTER_RSP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = 20 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDX };
        records[count++] = (X86Instruction){ .form = X86_FORM_SHR_RM32_IMM8, .rm = X86_REGISTER_RAX, .immediate = 2 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM8_R8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = 50 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = destination, .rm = X86_REGISTER_RBP,
                                            .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16 };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_LDTILECFG_M512, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
    if (dot)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 0, .rm = X86_REGISTER_RCX,
                                            .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
        records[count++] = (X86Instruction){ .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_R8,
                                            .index = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
        records[count++] = (X86Instruction){ .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 2, .rm = X86_REGISTER_R9,
                                            .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
        records[count++] = (X86Instruction){ .form = info->amx_form, .reg = 0, .rm = 1, .vvvv = 2 };
    }
    else if (info->amx == CBUILTIN_AMX_ZERO)
    {
        records[count++] = (X86Instruction){ .form = info->amx_form, .reg = 0 };
    }
    else
    {
        const uint8_t base = store ? X86_REGISTER_R8 : X86_REGISTER_RDX;
        const uint8_t stride = store || row ? X86_REGISTER_RSI : X86_REGISTER_RCX;
        records[count++] = (X86Instruction){ .form = info->amx == CBUILTIN_AMX_LOAD ? info->amx_form : X86_FORM_TILELOADD_TMM_SIBMEM,
                                            .reg = 0, .rm = base, .index = stride, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
    }
    if (row)
    {
        records[count++] = (X86Instruction){ .form = info->amx_form, .reg = 0, .rm = 0, .vvvv = X86_REGISTER_RCX };
        records[count++] = (X86Instruction){ .form = X86_FORM_VMOVDQU64_ZM512_Z, .reg = 0, .rm = destination, .mode = BUSTER_X86_MEMORY_BASE };
    }
    else
    {
        if (!store)
        {
            // All inputs are now in tiles. The string store can clobber rdi/rcx/rax while keeping the result pointer
            // and n (rsi) live. SysV requires DF clear on entry and return.
            records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RDI, .rm = destination };
            records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RCX, .immediate = 128 };
            records[count++] = (X86Instruction){ .form = X86_FORM_XOR_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX };
            records[count++] = (X86Instruction){ .form = X86_FORM_REP_STOS_Q };
        }
        records[count++] = (X86Instruction){ .form = X86_FORM_TILESTORED_SIBMEM_TMM, .reg = 0,
                                            .rm = store ? X86_REGISTER_RDX : destination,
                                            .index = store ? X86_REGISTER_RCX : X86_REGISTER_RSI,
                                            .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_TILERELEASE };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RBP, .rm = X86_REGISTER_RSP };
    records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RBP };
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    X86Instruction* owned = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, count);
    if (owned == NULL)
    {
        result.code.error = TIER0_ERROR_MEMORY;
    }
    else
    {
        memcpy(owned, records, (uint64_t)count * sizeof(*owned));
        result.code.records = owned;
        result.code.count = count;
        result.code.frame_pointer = true;
        result.code.frame_size = 64;
        result.code.frame_alignment_log2 = 6;
        result.parameter_count = info->parameter_count;
        result.result_class = BUSTER_TYPED_CLASS_VOID;
    }
    return result;
}

#endif
