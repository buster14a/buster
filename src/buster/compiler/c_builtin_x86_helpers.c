// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's x86 builtins that no Tier 0 row takes (c_check_program_x86.c), as per-unit leaves (c_check_program_helpers.c):
// their instructions name fixed registers (rdtsc and xgetbv write edx:eax, monitorx reads rax, ecx and edx, mwaitx eax,
// ebx and ecx, sha256rnds2 xmm0) or have legacy encodings (pause, and tzcnt). Each leaf but sha256rnds2's takes its
// arguments as the helpers' raw SysV integers (unsigned long, in rdi, rsi and rdx) and returns its result in rax, an
// unsigned long the caller converts:
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
//   the leaf's unsigned long), so bsf of its 32 bits finds its zero;
// - __builtin_ia32_lzcnt_u32 and _u64: the leading zeros' count, the width for 0, from bsr and cmov on any x86-64 (lzcnt
//   is bsr without LZCNT): the highest set bit's index b, of which the count is the width less one less b, its xor with
//   that, which takes 2 * width - 1 for 0 to the width;
// - __builtin_rotateleft8 to 64 and __builtin_rotateright8 to 64: rol and ror by cl, which takes the count modulo the
//   width (the 8- and 16-bit ones' count masked to 5 bits, a multiple of their widths), the result in the low bits;
// - the XSAVE family's saves and restores (xsave, xsavec, xsaveopt, xsaves, xrstor, xrstors and their 64-bit forms) of
//   the state at the address the mask's edx:eax names, and fxrstor's of the x87 and SSE state: a restore writes the
//   vector registers, which a caller's values aren't kept in across a call, as they are across a row;
// - __builtin_ia32_rdpkru and wrpkru (ecx and, for wrpkru, edx 0), rdpmc and rdpru of the counter or register ecx names,
//   edx:eax as one value, and rdtscp's, with the processor's id stored at its argument;
// - __builtin_ia32_readeflags_u64 and writeeflags_u64: pushfq and popfq, in a frame of rbp (Tier0Code.frame_pointer),
//   whose CFA the pushes don't move;
// - __builtin_ia32_clzero (rax's line), monitor (rax, ecx and edx), mwait (eax and ecx), vzeroall and vzeroupper, whose
//   registers no value of the caller keeps across the call;
// - __builtin_ia32_sha256rnds2(a, b, k): sha256rnds2 of a's state and b's, whose third operand, the round keys k, is
//   xmm0. Its vectors come and go as a function's of __v4si do (CBuiltinX86HelperInfo.vector): a, b and k in xmm0, xmm1
//   and xmm2, the result in xmm0.
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
    CBUILTIN_X86_HELPER_LZCNT32,
    CBUILTIN_X86_HELPER_LZCNT64,
    CBUILTIN_X86_HELPER_ROTATELEFT8,
    CBUILTIN_X86_HELPER_ROTATELEFT16,
    CBUILTIN_X86_HELPER_ROTATELEFT32,
    CBUILTIN_X86_HELPER_ROTATELEFT64,
    CBUILTIN_X86_HELPER_ROTATERIGHT8,
    CBUILTIN_X86_HELPER_ROTATERIGHT16,
    CBUILTIN_X86_HELPER_ROTATERIGHT32,
    CBUILTIN_X86_HELPER_ROTATERIGHT64,
    CBUILTIN_X86_HELPER_XSAVE,
    CBUILTIN_X86_HELPER_XSAVE64,
    CBUILTIN_X86_HELPER_XRSTOR,
    CBUILTIN_X86_HELPER_XRSTOR64,
    CBUILTIN_X86_HELPER_XSAVEC,
    CBUILTIN_X86_HELPER_XSAVEC64,
    CBUILTIN_X86_HELPER_XSAVEOPT,
    CBUILTIN_X86_HELPER_XSAVEOPT64,
    CBUILTIN_X86_HELPER_XSAVES,
    CBUILTIN_X86_HELPER_XSAVES64,
    CBUILTIN_X86_HELPER_XRSTORS,
    CBUILTIN_X86_HELPER_XRSTORS64,
    CBUILTIN_X86_HELPER_FXRSTOR,
    CBUILTIN_X86_HELPER_FXRSTOR64,
    CBUILTIN_X86_HELPER_RDPKRU,
    CBUILTIN_X86_HELPER_WRPKRU,
    CBUILTIN_X86_HELPER_RDPMC,
    CBUILTIN_X86_HELPER_RDPRU,
    CBUILTIN_X86_HELPER_RDTSCP,
    CBUILTIN_X86_HELPER_READEFLAGS,
    CBUILTIN_X86_HELPER_WRITEEFLAGS,
    CBUILTIN_X86_HELPER_CLZERO,
    CBUILTIN_X86_HELPER_MONITOR,
    CBUILTIN_X86_HELPER_MWAIT,
    CBUILTIN_X86_HELPER_VZEROALL,
    CBUILTIN_X86_HELPER_VZEROUPPER,
    CBUILTIN_X86_HELPER_SHA256RNDS2,
    CBUILTIN_X86_HELPER_COUNT,
} CBuiltinX86HelperKind;

typedef struct CBuiltinX86Helper CBuiltinX86Helper;
struct CBuiltinX86Helper
{
    Tier0Code code;
    uint32_t parameter_count;
    // U64, VECTOR for a vector leaf's (CBuiltinX86HelperInfo.vector), or VOID for a leaf of no result.
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

static const X86Instruction cbuiltin_x86_helper_lzcnt32[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_BSR_R32_RM32 },
    { .immediate = 63, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R32_RM32 },
    { .immediate = 31, .rm = X86_REGISTER_RAX, .form = X86_FORM_XOR_RM32_IMM8 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_lzcnt64[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_BSR_R64_RM64 },
    { .immediate = 127, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R64_RM64 },
    { .immediate = 63, .rm = X86_REGISTER_RAX, .form = X86_FORM_XOR_RM32_IMM8 },
    { .form = X86_FORM_RET },
};

// A rotate's leaf: the count in cl and the value in rax's low bits, rotated there by `instruction`.
#define BUSTER_CBUILTIN_X86_ROTATE(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 }, \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R64_RM64 }, \
        { .rm = X86_REGISTER_RAX, .form = instruction }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateleft8, X86_FORM_ROL_RM8_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateleft16, X86_FORM_ROL_RM16_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateleft32, X86_FORM_ROL_RM32_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateleft64, X86_FORM_ROL_RM64_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateright8, X86_FORM_ROR_RM8_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateright16, X86_FORM_ROR_RM16_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateright32, X86_FORM_ROR_RM32_CL)
BUSTER_CBUILTIN_X86_ROTATE(cbuiltin_x86_helper_rotateright64, X86_FORM_ROR_RM64_CL)
#undef BUSTER_CBUILTIN_X86_ROTATE

// An XSAVE instruction's leaf: the mask's halves in edx and eax, the state at rdi's address.
#define BUSTER_CBUILTIN_X86_XSAVE(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R64_RM64 }, \
        { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R64_RM64 }, \
        { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHR_RM64_IMM8 }, \
        { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = instruction }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsave, X86_FORM_XSAVE_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsave64, X86_FORM_XSAVE64_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xrstor, X86_FORM_XRSTOR_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xrstor64, X86_FORM_XRSTOR64_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsavec, X86_FORM_XSAVEC_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsavec64, X86_FORM_XSAVEC64_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsaveopt, X86_FORM_XSAVEOPT_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsaveopt64, X86_FORM_XSAVEOPT64_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsaves, X86_FORM_XSAVES_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xsaves64, X86_FORM_XSAVES64_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xrstors, X86_FORM_XRSTORS_M)
BUSTER_CBUILTIN_X86_XSAVE(cbuiltin_x86_helper_xrstors64, X86_FORM_XRSTORS64_M)
#undef BUSTER_CBUILTIN_X86_XSAVE

static const X86Instruction cbuiltin_x86_helper_fxrstor[] = {
    { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_FXRSTOR_M },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_fxrstor64[] = {
    { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_FXRSTOR64_M },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_rdpkru[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RCX, .form = X86_FORM_XOR_RM32_R32 },
    { .form = X86_FORM_RDPKRU },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_wrpkru[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RCX, .form = X86_FORM_XOR_RM32_R32 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDX, .form = X86_FORM_XOR_RM32_R32 },
    { .form = X86_FORM_WRPKRU },
    { .form = X86_FORM_RET },
};

// A counter's leaf: ecx's counter (or register), edx:eax as one value.
#define BUSTER_CBUILTIN_X86_COUNTER(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 }, \
        { .form = instruction }, \
        { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHL_RM64_IMM8 }, \
        { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .form = X86_FORM_OR_RM64_R64 }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_COUNTER(cbuiltin_x86_helper_rdpmc, X86_FORM_RDPMC)
BUSTER_CBUILTIN_X86_COUNTER(cbuiltin_x86_helper_rdpru, X86_FORM_RDPRU)
#undef BUSTER_CBUILTIN_X86_COUNTER

static const X86Instruction cbuiltin_x86_helper_rdtscp[] = {
    { .form = X86_FORM_RDTSCP },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_RM32_R32 },
    { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHL_RM64_IMM8 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .form = X86_FORM_OR_RM64_R64 },
    { .form = X86_FORM_RET },
};

// The flags' leaves, in a frame of rbp: unwind_tier0_frame's prologue (push rbp and its mov) and epilogue (rsp's mov
// from rbp, pop rbp and ret) around the pushes, so that an unwinder reads the CFA from rbp between them.
static const X86Instruction cbuiltin_x86_helper_readeflags[] = {
    { .rm = X86_REGISTER_RBP, .form = X86_FORM_PUSH_R64 },
    { .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP, .form = X86_FORM_MOV_RM64_R64 },
    { .form = X86_FORM_PUSHFQ },
    { .rm = X86_REGISTER_RAX, .form = X86_FORM_POP_R64 },
    { .reg = X86_REGISTER_RBP, .rm = X86_REGISTER_RSP, .form = X86_FORM_MOV_RM64_R64 },
    { .rm = X86_REGISTER_RBP, .form = X86_FORM_POP_R64 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_writeeflags[] = {
    { .rm = X86_REGISTER_RBP, .form = X86_FORM_PUSH_R64 },
    { .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP, .form = X86_FORM_MOV_RM64_R64 },
    { .rm = X86_REGISTER_RDI, .form = X86_FORM_PUSH_R64 },
    { .form = X86_FORM_POPFQ },
    { .reg = X86_REGISTER_RBP, .rm = X86_REGISTER_RSP, .form = X86_FORM_MOV_RM64_R64 },
    { .rm = X86_REGISTER_RBP, .form = X86_FORM_POP_R64 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_clzero[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R64_RM64 },
    { .form = X86_FORM_CLZERO },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_monitor[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R64_RM64 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_MONITOR },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_mwait[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_MWAIT },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_vzeroall[] = {
    { .form = X86_FORM_VZEROALL },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_vzeroupper[] = {
    { .form = X86_FORM_VZEROUPPER },
    { .form = X86_FORM_RET },
};

// a (xmm0) to xmm3, k (xmm2) to xmm0, the rounds into xmm3, and xmm3 to xmm0.
static const X86Instruction cbuiltin_x86_helper_sha256rnds2[] = {
    { .reg = 3, .rm = 0, .form = X86_FORM_MOVAPS_X_XM },
    { .reg = 0, .rm = 2, .form = X86_FORM_MOVAPS_X_XM },
    { .reg = 3, .rm = 1, .form = X86_FORM_SHA256RNDS2_X_XM128_XMM0 },
    { .reg = 0, .rm = 3, .form = X86_FORM_MOVAPS_X_XM },
    { .form = X86_FORM_RET },
};

// Each kind's builtin's name, its parameters, whether it has a result, its records, the target's features it takes and
// the refusal on a target without them, its frame (the callee-saved registers it pushes, Tier0Frame.saved's, and whether
// it keeps rbp's frame), and whether its parameters and result are its builtin's 16-byte vectors, which a call passes in
// the XMM registers and returns in xmm0, rather than unsigned longs.
typedef struct CBuiltinX86HelperInfo CBuiltinX86HelperInfo;
struct CBuiltinX86HelperInfo
{
    const char* name;
    uint32_t name_length;
    uint32_t parameter_count;
    bool result;
    const X86Instruction* records;
    uint32_t record_count;
    uint64_t features;
    const char* refusal;
    uint32_t saved;
    bool pointer;
    bool vector;
};

#define BUSTER_CBUILTIN_X86_LEAF(id, ...) [id] = { __VA_ARGS__ },
static const CBuiltinX86HelperInfo cbuiltin_x86_helper_infos[CBUILTIN_X86_HELPER_COUNT] = {
#include "c_builtin_x86_leaves.inc"
};
#undef BUSTER_CBUILTIN_X86_LEAF

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
            result.code.frame_saved = info->saved;
            result.code.frame_pointer = info->pointer;
            result.parameter_count = info->parameter_count;
            result.result_class = info->vector ? BUSTER_TYPED_CLASS_VECTOR : info->result ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
        }
    }
    return result;
}

#endif
