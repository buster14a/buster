// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's x86 builtins that no Tier 0 row takes (c_check_program_x86.c), as per-unit leaves (c_check_program_helpers.c):
// their instructions name fixed registers (rdtsc and xgetbv write edx:eax, monitorx reads rax, ecx and edx, mwaitx eax,
// ebx and ecx) or have legacy encodings (pause, and tzcnt). Each leaf takes its arguments as the helpers' raw SysV
// integers (unsigned long, in rdi, rsi and rdx) and returns its result in rax, an unsigned long the caller converts:
//
// - __rdtsc: rdtsc's edx:eax as one value;
// - __builtin_ia32_pause: pause;
// - __builtin_ia32_xgetbv(unsigned): the extended control register ecx names, edx:eax as one value;
// - __builtin_ia32_monitorx(const void*, unsigned, unsigned): monitorx of the address (rax) with its extensions (ecx)
//   and hints (edx);
// - __builtin_ia32_mwaitx(unsigned, unsigned, unsigned): mwaitx with its extensions (ecx), hints (eax) and the timer's
//   value (ebx, which it reads where the extensions enable the timer): rbx is callee-saved, so the leaf pushes it first
//   and pops it before its ret, a frame its unwind table describes (Tier0Code.frame_saved);
// - __builtin_ia32_tzcnt_u16, _u32 and _u64: the trailing zeros' count, the width for 0 (llvm.cttz's defined zero case,
//   which clang lowers the builtin to), from bsf and cmov, so that the leaf gives it on any x86-64 (tzcnt is bsf without
//   BMI1, which leaves its destination undefined for 0). A 16-bit argument arrives zero-extended (the call converts it to
//   the leaf's unsigned long), so bsf of its 32 bits finds its zero.
//
// Output owns exactly the leaf's records; invalid kinds, malformed arenas and exhaustion leave the arena as it was.
// Include after tier0.c.
#ifndef BUSTER_C_BUILTIN_X86_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_X86_HELPERS_INCLUDED

typedef enum CBuiltinX86HelperKind
{
    CBUILTIN_X86_HELPER_NONE,
    CBUILTIN_X86_HELPER_RDTSC,
    CBUILTIN_X86_HELPER_PAUSE,
    CBUILTIN_X86_HELPER_XGETBV,
    CBUILTIN_X86_HELPER_MONITORX,
    CBUILTIN_X86_HELPER_MWAITX,
    CBUILTIN_X86_HELPER_TZCNT16,
    CBUILTIN_X86_HELPER_TZCNT32,
    CBUILTIN_X86_HELPER_TZCNT64,
    CBUILTIN_X86_HELPER_COUNT,
} CBuiltinX86HelperKind;

typedef struct CBuiltinX86Helper CBuiltinX86Helper;
struct CBuiltinX86Helper
{
    Tier0Code code;
    uint32_t parameter_count;
    // U64, or VOID for a leaf of no result.
    uint32_t result_class;
};

static const X86Instruction cbuiltin_x86_helper_rdtsc[] = {
    { .form = X86_FORM_RDTSC },
    { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHL_RM64_IMM8 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .form = X86_FORM_OR_RM64_R64 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_pause[] = {
    { .form = X86_FORM_PAUSE },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_xgetbv[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_XGETBV },
    { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHL_RM64_IMM8 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .form = X86_FORM_OR_RM64_R64 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_monitorx[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R64_RM64 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_MONITORX },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_mwaitx[] = {
    { .rm = X86_REGISTER_RBX, .form = X86_FORM_PUSH_R64 },
    { .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RDX, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_MWAITX },
    { .rm = X86_REGISTER_RBX, .form = X86_FORM_POP_R64 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_tzcnt16[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_BSF_R32_RM32 },
    { .immediate = 16, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R32_RM32 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_tzcnt32[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_BSF_R32_RM32 },
    { .immediate = 32, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R32_RM32 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_tzcnt64[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_BSF_R64_RM64 },
    { .immediate = 64, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R64_RM64 },
    { .form = X86_FORM_RET },
};

// Each kind's builtin's name, its parameters, whether it has a result, and its records.
typedef struct CBuiltinX86HelperInfo CBuiltinX86HelperInfo;
struct CBuiltinX86HelperInfo
{
    const char* name;
    uint32_t name_length;
    uint32_t parameter_count;
    bool result;
    const X86Instruction* records;
    uint32_t record_count;
};

#define BUSTER_CBUILTIN_X86_HELPER(name, parameters, result, records) { name, sizeof(name) - 1, parameters, result, records, BUSTER_ARRAY_LENGTH(records) }

static const CBuiltinX86HelperInfo cbuiltin_x86_helper_infos[CBUILTIN_X86_HELPER_COUNT] = {
    [CBUILTIN_X86_HELPER_RDTSC] = BUSTER_CBUILTIN_X86_HELPER("__rdtsc", 0, true, cbuiltin_x86_helper_rdtsc),
    [CBUILTIN_X86_HELPER_PAUSE] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_pause", 0, false, cbuiltin_x86_helper_pause),
    [CBUILTIN_X86_HELPER_XGETBV] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xgetbv", 1, true, cbuiltin_x86_helper_xgetbv),
    [CBUILTIN_X86_HELPER_MONITORX] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_monitorx", 3, false, cbuiltin_x86_helper_monitorx),
    [CBUILTIN_X86_HELPER_MWAITX] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_mwaitx", 3, false, cbuiltin_x86_helper_mwaitx),
    [CBUILTIN_X86_HELPER_TZCNT16] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u16", 1, true, cbuiltin_x86_helper_tzcnt16),
    [CBUILTIN_X86_HELPER_TZCNT32] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u32", 1, true, cbuiltin_x86_helper_tzcnt32),
    [CBUILTIN_X86_HELPER_TZCNT64] = BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u64", 1, true, cbuiltin_x86_helper_tzcnt64),
};

// The kind of a builtin's name, CBUILTIN_X86_HELPER_NONE for a builtin no leaf computes.
static CBuiltinX86HelperKind cbuiltin_x86_helper_kind(const char* name, uint32_t length)
{
    CBuiltinX86HelperKind result = CBUILTIN_X86_HELPER_NONE;
    for (uint32_t k = CBUILTIN_X86_HELPER_NONE + 1; name != NULL && result == CBUILTIN_X86_HELPER_NONE && k < CBUILTIN_X86_HELPER_COUNT; k += 1)
    {
        const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + k;
        result = info->name_length == length && memcmp(info->name, name, length) == 0 ? (CBuiltinX86HelperKind)k : result;
    }
    return result;
}

static CBuiltinX86Helper cbuiltin_x86_helper(CBuiltinX86HelperKind kind, Arena* output)
{
    CBuiltinX86Helper result = { 0 };
    if (kind <= CBUILTIN_X86_HELPER_NONE || kind >= CBUILTIN_X86_HELPER_COUNT)
    {
        result.code.error = TIER0_ERROR_UNSUPPORTED;
    }
    else if (output == NULL || output->length > output->capacity || (output->base == NULL && output->capacity != 0))
    {
        result.code.error = TIER0_ERROR_INVALID;
    }
    else
    {
        const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + kind;
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, info->record_count);
        if (records == NULL)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            memcpy(records, info->records, (uint64_t)info->record_count * sizeof(*records));
            result.code.records = records;
            result.code.count = info->record_count;
            // mwaitx's leaf saves rbx (Tier0Frame.saved's first register) around its body.
            result.code.frame_saved = kind == CBUILTIN_X86_HELPER_MWAITX ? 1u : 0u;
            result.parameter_count = info->parameter_count;
            result.result_class = info->result ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
        }
    }
    return result;
}

#endif
