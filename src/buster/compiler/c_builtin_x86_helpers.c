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
//   xmm0. Its vectors come and go as a function's of __v4si do (CBuiltinX86HelperInfo.vectors): a, b and k in xmm0,
//   xmm1 and xmm2, the result in xmm0;
// - Key Locker's __builtin_ia32_aesenc128kl_u8, aesdec128kl, aesenc256kl and aesdec256kl (odata, idata, h): the rounds
//   of idata (a vector, in xmm0) with the key of the handle at h, stored at odata where ZF says the handle was valid
//   (0), and zeros otherwise; ZF the result. That is how Intel's manual and clang's own header (keylockerintrin.h)
//   describe them and how GCC compiles them; clang's code (23, and LLVM's main) stores the result where ZF is 1 and
//   zeros where it is 0, which test-x86-intrinsics compares with clang's conditions negated. The leaf's jne over the
//   zeroing is its branch (CBuiltinX86HelperInfo.branch), which branch sizing sizes;
// - their wide forms, __builtin_ia32_aesencwide128kl_u8 to aesdecwide256kl (odata, idata, h): the same of the 8 blocks
//   at idata, in xmm0 to xmm7;
// - __builtin_ia32_encodekey128_u32 and encodekey256 (htype, key, h) and (htype, key_lo, key_hi, h): the handle of the
//   key in xmm0 (and xmm1), wrapped as htype says, stored at h from xmm0 to xmm2 (or xmm3); its information the result;
// - __builtin_ia32_loadiwkey (intkey, enkey_lo, enkey_hi, ctl): the internal wrapping key from xmm0 to xmm2, as ctl
//   (eax) says;
// - __builtin_ia32_umwait and tpause (control, hi, lo): the wait control (ecx) asks for, until the TSC passes hi:lo
//   (edx:eax); CF, which says whether the OS's time limit ended it, the result;
// - __builtin_ia32_xbegin: a transaction's start, its fallback the next instruction (xbegin's displacement 0), the
//   leaf's ret: eax is -1 where the transaction runs, and its abort status where it aborts.
//
// Output owns exactly the leaf's records and its branch columns; invalid kinds, malformed arenas and exhaustion leave
// the arena as it was. Include after tier0.c.
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
    CBUILTIN_X86_HELPER_AESENC128KL,
    CBUILTIN_X86_HELPER_AESDEC128KL,
    CBUILTIN_X86_HELPER_AESENC256KL,
    CBUILTIN_X86_HELPER_AESDEC256KL,
    CBUILTIN_X86_HELPER_AESENCWIDE128KL,
    CBUILTIN_X86_HELPER_AESDECWIDE128KL,
    CBUILTIN_X86_HELPER_AESENCWIDE256KL,
    CBUILTIN_X86_HELPER_AESDECWIDE256KL,
    CBUILTIN_X86_HELPER_ENCODEKEY128,
    CBUILTIN_X86_HELPER_ENCODEKEY256,
    CBUILTIN_X86_HELPER_LOADIWKEY,
    CBUILTIN_X86_HELPER_UMWAIT,
    CBUILTIN_X86_HELPER_TPAUSE,
    CBUILTIN_X86_HELPER_XBEGIN,
    CBUILTIN_X86_HELPER_RDTSC_BUILTIN,
    CBUILTIN_X86_HELPER_LZCNT16,
    CBUILTIN_X86_HELPER_LFENCE,
    CBUILTIN_X86_HELPER_MFENCE,
    CBUILTIN_X86_HELPER_SFENCE,
    CBUILTIN_X86_HELPER_CLFLUSH,
    CBUILTIN_X86_HELPER_LDMXCSR,
    CBUILTIN_X86_HELPER_STMXCSR,
    CBUILTIN_X86_HELPER_XSETBV,
    CBUILTIN_X86_HELPER_MM_LFENCE,
    CBUILTIN_X86_HELPER_MM_MFENCE,
    CBUILTIN_X86_HELPER_MM_SFENCE,
    CBUILTIN_X86_HELPER_MM_PAUSE,
    CBUILTIN_X86_HELPER_MM_CLFLUSH,
    CBUILTIN_X86_HELPER_MM_GETCSR,
    CBUILTIN_X86_HELPER_MM_SETCSR,
    CBUILTIN_X86_HELPER_M_PREFETCH,
    CBUILTIN_X86_HELPER_M_PREFETCHW,
    CBUILTIN_X86_HELPER_DEBUGTRAP,
    CBUILTIN_X86_HELPER_READCYCLECOUNTER,
    CBUILTIN_X86_HELPER_READSTEADYCOUNTER,
    CBUILTIN_X86_HELPER_THREAD_POINTER,
    CBUILTIN_X86_HELPER_STACK_ADDRESS,
    CBUILTIN_X86_HELPER_DWARF_SP_COLUMN,
    CBUILTIN_X86_HELPER_CLEAR_CACHE,
    CBUILTIN_X86_HELPER_ASSUME_SEPARATE_STORAGE,
    CBUILTIN_X86_HELPER_ASSUME_DEREFERENCEABLE,
    CBUILTIN_X86_HELPER_MEMCPY_INLINE,
    CBUILTIN_X86_HELPER_MEMSET_INLINE,
    CBUILTIN_X86_HELPER_TILEZERO,
    CBUILTIN_X86_HELPER_TILELOADD,
    CBUILTIN_X86_HELPER_TILELOADDT1,
    CBUILTIN_X86_HELPER_TILELOADDRS,
    CBUILTIN_X86_HELPER_TILELOADDRST1,
    CBUILTIN_X86_HELPER_TILESTORED,
    CBUILTIN_X86_HELPER_TDPBSSD,
    CBUILTIN_X86_HELPER_TDPBSUD,
    CBUILTIN_X86_HELPER_TDPBUSD,
    CBUILTIN_X86_HELPER_TDPBUUD,
    CBUILTIN_X86_HELPER_TDPBF16PS,
    CBUILTIN_X86_HELPER_TDPFP16PS,
    CBUILTIN_X86_HELPER_TCMMIMFP16PS,
    CBUILTIN_X86_HELPER_TCMMRLFP16PS,
    CBUILTIN_X86_HELPER_TDPBF8PS,
    CBUILTIN_X86_HELPER_TDPBHF8PS,
    CBUILTIN_X86_HELPER_TDPHBF8PS,
    CBUILTIN_X86_HELPER_TDPHF8PS,
    CBUILTIN_X86_HELPER_TCVTROWD2PS,
    CBUILTIN_X86_HELPER_TCVTROWPS2BF16H,
    CBUILTIN_X86_HELPER_TCVTROWPS2BF16L,
    CBUILTIN_X86_HELPER_TCVTROWPS2PHH,
    CBUILTIN_X86_HELPER_TCVTROWPS2PHL,
    CBUILTIN_X86_HELPER_TILEMOVROW,
    CBUILTIN_X86_HELPER_COUNT,
} CBuiltinX86HelperKind;

typedef struct CBuiltinX86Helper CBuiltinX86Helper;
struct CBuiltinX86Helper
{
    Tier0Code code;
    uint32_t parameter_count;
    // U64, VECTOR for a vector result's (CBuiltinX86HelperInfo.vectors), or VOID for a leaf of no result.
    uint32_t result_class;
};

static const X86Instruction cbuiltin_x86_helper_rdtsc[] = {
    { .form = X86_FORM_RDTSC },
    { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHL_RM64_IMM8 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .form = X86_FORM_OR_RM64_R64 },
    { .form = X86_FORM_RET },
};

// lzcnt of a's low 16 bits, 16 for 0: BSR's index of its top bit, xored with 15 (31's for 0, which BSR leaves alone).
static const X86Instruction cbuiltin_x86_helper_lzcnt16[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOVZX_R32_RM16 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RCX, .form = X86_FORM_BSR_R32_RM32 },
    { .immediate = 31, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .form = X86_FORM_CMOVNE_R32_RM32 },
    { .immediate = 15, .rm = X86_REGISTER_RAX, .form = X86_FORM_XOR_RM32_IMM8 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_lfence[] = {
    { .form = X86_FORM_LFENCE },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_mfence[] = {
    { .form = X86_FORM_MFENCE },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_sfence[] = {
    { .form = X86_FORM_SFENCE },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_clflush[] = {
    { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_CLFLUSH_M8 },
    { .form = X86_FORM_RET },
};

// LDMXCSR and STMXCSR take memory: the leaf's red zone below rsp, which a leaf may use.
static const X86Instruction cbuiltin_x86_helper_ldmxcsr[] = {
    { .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -4, .form = X86_FORM_MOV_RM32_R32 },
    { .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -4, .form = X86_FORM_LDMXCSR_M32 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_stmxcsr[] = {
    { .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -4, .form = X86_FORM_STMXCSR_M32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -4, .form = X86_FORM_MOV_R32_RM32 },
    { .form = X86_FORM_RET },
};

// _m_prefetch's and _m_prefetchw's lines at p, as clang's code prefetches them: PREFETCHT0 (a read, locality 3) and
// PREFETCHW.
static const X86Instruction cbuiltin_x86_helper_prefetch[] = {
    { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_PREFETCHT0_M8 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_prefetchw[] = {
    { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_PREFETCHW_M8 },
    { .form = X86_FORM_RET },
};

// The generic builtins whose code is an instruction or two of clang -O0's (#148), each a leaf, as the system leaves are:
// __builtin_debugtrap's int3, whose trap returns past it; __builtin_readsteadycounter's 0, which x86's code reads no
// counter for; __builtin_thread_pointer's fs:[0], the thread pointer at its own address; __builtin_stack_address's rsp
// at the call, past the leaf's return address; and __builtin_dwarf_sp_column's 7, rsp's DWARF register.
// __builtin___clear_cache, __builtin_assume_separate_storage and __builtin_assume_dereferenceable compute nothing past
// their arguments, which the call evaluates as clang's does; __builtin_memcpy_inline and __builtin_memset_inline copy and
// fill their constant size's bytes, which clang's code does with stores of its own, by rep movsb and rep stosb.
static const X86Instruction cbuiltin_x86_helper_debugtrap[] = {
    { .form = X86_FORM_INT3 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_zero[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .form = X86_FORM_XOR_R32_RM32 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_thread_pointer[] = {
    { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_stack_address[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8, .form = X86_FORM_LEA_R64_M },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_dwarf_sp_column[] = {
    { .immediate = 7, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_nothing[] = {
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_memcpy_inline[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .form = X86_FORM_MOV_R64_RM64 },
    { .form = X86_FORM_REP_MOVS_B },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_memset_inline[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .form = X86_FORM_MOV_R64_RM64 },
    { .form = X86_FORM_REP_STOS_B },
    { .form = X86_FORM_RET },
};

// xsetbv(a, v): XCR a (ecx) set to v's halves, edx:eax.
static const X86Instruction cbuiltin_x86_helper_xsetbv[] = {
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R64_RM64 },
    { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R64_RM64 },
    { .immediate = 32, .rm = X86_REGISTER_RDX, .form = X86_FORM_SHR_RM64_IMM8 },
    { .form = X86_FORM_XSETBV },
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

// A Key Locker leaf: the rounds of idata (xmm0) with the handle at h (rsi), and where ZF is 1 (the handle isn't valid)
// zeros (the jne at record 1 skips them, to record 3); ZF's boolean in eax, and the block stored at odata (rdi).
#define BUSTER_CBUILTIN_X86_KEY_LOCKER(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = 0, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .form = instruction }, \
        { .form = X86_FORM_JNE_REL8 }, \
        { .reg = 0, .rm = 0, .form = X86_FORM_PXOR_X_XM128 }, \
        { .rm = X86_REGISTER_RAX, .form = X86_FORM_SETE_RM8 }, \
        { .reg = 0, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOVZX_R32_RM8 }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_KEY_LOCKER(cbuiltin_x86_helper_aesenc128kl, X86_FORM_AESENC128KL_X_M384)
BUSTER_CBUILTIN_X86_KEY_LOCKER(cbuiltin_x86_helper_aesdec128kl, X86_FORM_AESDEC128KL_X_M384)
BUSTER_CBUILTIN_X86_KEY_LOCKER(cbuiltin_x86_helper_aesenc256kl, X86_FORM_AESENC256KL_X_M512)
BUSTER_CBUILTIN_X86_KEY_LOCKER(cbuiltin_x86_helper_aesdec256kl, X86_FORM_AESDEC256KL_X_M512)
#undef BUSTER_CBUILTIN_X86_KEY_LOCKER

// A wide Key Locker leaf: the 8 blocks at idata (rsi) into xmm0 to xmm7, their rounds with the handle at h (rdx), zeros
// where ZF is 1 (the jne at record 9 skips them, to record 18), ZF's boolean in eax, and the blocks stored at odata
// (rdi).
#define BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = 0, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 1, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 2, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 3, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 48, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 4, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 5, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 80, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 6, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 96, .form = X86_FORM_MOVDQU_X_XM }, \
        { .reg = 7, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 112, .form = X86_FORM_MOVDQU_X_XM }, \
        { .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE, .form = instruction }, \
        { .form = X86_FORM_JNE_REL8 }, \
        { .reg = 0, .rm = 0, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 1, .rm = 1, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 2, .rm = 2, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 3, .rm = 3, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 4, .rm = 4, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 5, .rm = 5, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 6, .rm = 6, .form = X86_FORM_PXOR_X_XM128 }, \
        { .reg = 7, .rm = 7, .form = X86_FORM_PXOR_X_XM128 }, \
        { .rm = X86_REGISTER_RAX, .form = X86_FORM_SETE_RM8 }, \
        { .reg = 0, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 1, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 2, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 3, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 48, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 4, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 5, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 80, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 6, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 96, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = 7, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 112, .form = X86_FORM_MOVDQU_XM_X }, \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOVZX_R32_RM8 }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE(cbuiltin_x86_helper_aesencwide128kl, X86_FORM_AESENCWIDE128KL_M384)
BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE(cbuiltin_x86_helper_aesdecwide128kl, X86_FORM_AESDECWIDE128KL_M384)
BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE(cbuiltin_x86_helper_aesencwide256kl, X86_FORM_AESENCWIDE256KL_M512)
BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE(cbuiltin_x86_helper_aesdecwide256kl, X86_FORM_AESDECWIDE256KL_M512)
#undef BUSTER_CBUILTIN_X86_KEY_LOCKER_WIDE

// htype (edi) wrapped with the key in xmm0 (and xmm1) into a handle in xmm0 to xmm2 (or xmm3), stored at h (rsi); the
// handle's information in eax.
static const X86Instruction cbuiltin_x86_helper_encodekey128[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_ENCODEKEY128_R32_R32 },
    { .reg = 0, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOVDQU_XM_X },
    { .reg = 1, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16, .form = X86_FORM_MOVDQU_XM_X },
    { .reg = 2, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32, .form = X86_FORM_MOVDQU_XM_X },
    { .form = X86_FORM_RET },
};

static const X86Instruction cbuiltin_x86_helper_encodekey256[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_ENCODEKEY256_R32_R32 },
    { .reg = 0, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOVDQU_XM_X },
    { .reg = 1, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16, .form = X86_FORM_MOVDQU_XM_X },
    { .reg = 2, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32, .form = X86_FORM_MOVDQU_XM_X },
    { .reg = 3, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 48, .form = X86_FORM_MOVDQU_XM_X },
    { .form = X86_FORM_RET },
};

// The integrity key in xmm0, the encryption key's halves in xmm1 and xmm2, and ctl (edi) in eax.
static const X86Instruction cbuiltin_x86_helper_loadiwkey[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 },
    { .reg = 1, .rm = 2, .form = X86_FORM_LOADIWKEY_X_X },
    { .form = X86_FORM_RET },
};

// The wait's leaf: control (edi) in ecx and the deadline's halves, hi (esi) and lo (edx), in edx:eax, lo first; CF's
// boolean in eax.
#define BUSTER_CBUILTIN_X86_WAIT(name, instruction) \
    static const X86Instruction name[] = { \
        { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDI, .form = X86_FORM_MOV_R32_RM32 }, \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDX, .form = X86_FORM_MOV_R32_RM32 }, \
        { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RSI, .form = X86_FORM_MOV_R32_RM32 }, \
        { .rm = X86_REGISTER_RCX, .form = instruction }, \
        { .rm = X86_REGISTER_RAX, .form = X86_FORM_SETB_RM8 }, \
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOVZX_R32_RM8 }, \
        { .form = X86_FORM_RET }, \
    };
BUSTER_CBUILTIN_X86_WAIT(cbuiltin_x86_helper_umwait, X86_FORM_UMWAIT_R32)
BUSTER_CBUILTIN_X86_WAIT(cbuiltin_x86_helper_tpause, X86_FORM_TPAUSE_R32)
#undef BUSTER_CBUILTIN_X86_WAIT

// -1 in eax, then xbegin, whose fallback is the ret after it: where the transaction aborts, the processor puts its
// status in eax and goes on there.
static const X86Instruction cbuiltin_x86_helper_xbegin[] = {
    { .immediate = -1, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
    { .form = X86_FORM_XBEGIN_REL32 },
    { .form = X86_FORM_RET },
};

// Each kind's builtin's name, its parameters, whether it has a result, its records, the target's features it takes and
// the refusal on a target without them, its frame (the callee-saved registers it pushes, Tier0Frame.saved's, and whether
// it keeps rbp's frame), which of its parameters and whether its result are its builtin's 16-byte vectors, which a call
// passes in the XMM registers and returns in xmm0, rather than unsigned longs, the target's extensions it takes
// (X86Target.extensions), and its branch, a Jcc_REL8's record and its target's, where it has one (branch sizing's
// columns).
typedef enum CBuiltinAmxFamily
{
    CBUILTIN_AMX_NONE,
    CBUILTIN_AMX_ZERO,
    CBUILTIN_AMX_LOAD,
    CBUILTIN_AMX_STORE,
    CBUILTIN_AMX_DOT,
    CBUILTIN_AMX_ROW,
} CBuiltinAmxFamily;

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
    // Bit a for parameter a, and BUSTER_CBUILTIN_X86_VECTOR_RESULT for the result.
    uint8_t vectors;
    uint64_t extensions;
    uint32_t branch_count;
    uint32_t branch;
    uint32_t target;
    // AMX internal builtins use scalar dimensions and addresses, plus a final output address for a nonvoid result.
    // Their generated bodies configure the tile shapes locally and release them before returning.
    uint8_t amx;
    uint16_t amx_form;
    uint16_t output_bytes;
};
#define BUSTER_CBUILTIN_X86_VECTOR_RESULT 0x80u

#define BUSTER_CBUILTIN_X86_LEAF(id, ...) [id] = { __VA_ARGS__ },
#define BUSTER_CBUILTIN_AMX(id, builtin, family, parameters, bytes, form, extension) \
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_##id, \
        .name = "__builtin_ia32_" builtin "_internal", .name_length = sizeof("__builtin_ia32_" builtin "_internal") - 1, \
        .parameter_count = parameters, .amx = CBUILTIN_AMX_##family, .amx_form = X86_FORM_##form, .output_bytes = bytes, \
        .features = CBUILTIN_AMX_##family == CBUILTIN_AMX_ROW ? BUSTER_X86_FEATURE(AVX512F) : 0, \
        .extensions = BUSTER_X86_EXTENSION(AMXTILE) | BUSTER_X86_EXTENSION(extension) | \
                      (CBUILTIN_AMX_##family == CBUILTIN_AMX_ROW ? BUSTER_X86_EXTENSION(AVX10_2) : 0), \
        .refusal = "this AMX intrinsic without its required target features")
static const CBuiltinX86HelperInfo cbuiltin_x86_helper_infos[CBUILTIN_X86_HELPER_COUNT] = {
#include "c_builtin_x86_leaves.inc"
#include "c_builtin_amx_leaves.inc"
};
#undef BUSTER_CBUILTIN_AMX
#undef BUSTER_CBUILTIN_X86_LEAF

#include "c_builtin_amx_helpers.c"

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
        if (info->amx != 0)
        {
            result = cbuiltin_amx_helper(info, output);
        }
        else
        {
            const uint64_t length = output->length;
            X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, info->record_count);
            // The branch's columns: its record, then its target's.
            uint32_t* columns = records != NULL && info->branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, 2) : NULL;
            if (records == NULL || (info->branch_count != 0 && columns == NULL))
            {
                output->length = length;
                result.code.error = TIER0_ERROR_MEMORY;
            }
            else
            {
                memcpy(records, info->records, (uint64_t)info->record_count * sizeof(*records));
                result.code.records = records;
                result.code.count = info->record_count;
                if (columns != NULL)
                {
                    columns[0] = info->branch;
                    columns[1] = info->target;
                    result.code.branches = columns;
                    result.code.targets = columns + 1;
                    result.code.branch_count = info->branch_count;
                }
                result.code.frame_saved = info->saved;
                result.code.frame_pointer = info->pointer;
                result.parameter_count = info->parameter_count;
                result.result_class = (info->vectors & BUSTER_CBUILTIN_X86_VECTOR_RESULT) ? BUSTER_TYPED_CLASS_VECTOR : info->result ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
            }
        }
    }
    return result;
}

#endif
