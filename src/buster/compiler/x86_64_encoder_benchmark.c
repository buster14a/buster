// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers adapted through c_lexer_benchmark.c retain MIT and the upstream notice:
// Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// The x86-64 encoder benchmark: times x86_encode at each batch width (instructions per loop iteration) and output
// store, x86_encode_apx's loop at 64 and 16 per iteration (whose instructions here have no registers 16 to 31, and so
// the same bytes) and a one-instruction-at-a-time scalar loop, on a synthetic instruction stream, after checking that
// they all produce the same bytes, offsets and fixup ends; or with --mode32 or --mode16, the loops of 32-bit or 16-bit
// mode at 64 and 16 per iteration (x86_encode_mode32's or x86_encode_mode16's is the second) and the mode's scalar
// loop.
//
// Usage: build/compiler benchmark-x86-encoder [--count N] [--section N] [--uniform | --vector] [--mode32 | --mode16]
//                                             [--offsets] [--fixups] [--after-scalar MS] [--seed N] [--only NAME]
//
// The stream has --count instructions (BUSTER_X86_BENCHMARK_DEFAULT_COUNT by default): a mix shaped like compiler
// output (x86_benchmark_patterns: register moves, stack loads and stores, calls, branches, arithmetic, some SSE); with
// --vector, like vectorized compiler output (x86_benchmark_vector_patterns: AVX-512 with opmasks and broadcasts, AVX2,
// BMI and the loop around them); with --mode32, like 32-bit compiler output (x86_benchmark_patterns_32), and with
// --mode16, like 16-bit code (x86_benchmark_patterns_16); or with --uniform, random instructions of every form (in the
// mode). --section N encodes it in calls
// of N instructions, each appending to the same output, like a compiler encoding a function at a time; otherwise one
// call encodes it all. --offsets has every call store each instruction's offset too; --fixups marks the instructions a
// code generator would have fixups for (calls and RIP-relative operands, x86_benchmark_mark) and has every call
// report their ends; --after-scalar MS runs the scalar loop for MS milliseconds before every timed pass of the other
// implementations (x86_benchmark_after_scalar), so that each pass runs in the state a compiler's vector phases run in;
// and --only times just the implementation NAME (for perf stat, say). Run it pinned to one CPU (taskset -c 2 ...). The
// exit status is 1 if the arguments are invalid or an implementation's output differs from the scalar loop's.
//
// Layout, in this order:
//   - The stream: x86_benchmark_patterns, x86_benchmark_vector_patterns, x86_benchmark_patterns_32 and
//     x86_benchmark_patterns_16, pattern_instruction, make_stream (x86_64_encoder_test.c's generator isn't in this
//     translation unit, so the uniform stream has its own copy, uniform_instruction) and x86_benchmark_mark.
//   - The implementations: encode_scalar_loop (and encode_scalar_loop_32 and encode_scalar_loop_16),
//     encode_offsets_then_pick and x86_benchmark_implementations (and x86_benchmark_implementations_32 and
//     x86_benchmark_implementations_16).
//   - Timing: the perf counters (cycles, instructions and the power cap's throttle, BUSTER_X86_BENCHMARK_THROTTLE_EVENT),
//     encode_stream, x86_benchmark_after_scalar and run_x86_encoder_benchmark, which runs a warm-up pass and then
//     BUSTER_X86_BENCHMARK_PASSES passes with the implementations taking turns, and reports medians.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

#include "buster/compiler/x86_64_encoder.c"

#define BUSTER_X86_BENCHMARK_DEFAULT_COUNT 16384
#define BUSTER_X86_BENCHMARK_PASSES 21
// Each pass encodes the stream as many times as it takes to encode at least this many instructions, so that a pass
// is long next to the timer and counter reads.
#define BUSTER_X86_BENCHMARK_PASS_INSTRUCTIONS (4u << 20)
#define BUSTER_X86_BENCHMARK_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// --after-scalar's scalar loop encodes the stream's first instructions, this many per call, as the experiment copy of
// the benchmark that found the state did (#182), and runs for at most BUSTER_X86_BENCHMARK_AFTER_SCALAR_MAX_MS.
#define BUSTER_X86_BENCHMARK_AFTER_SCALAR_INSTRUCTIONS 256
#define BUSTER_X86_BENCHMARK_AFTER_SCALAR_MAX_MS 10000
// PMCx1A0 (dispatch slots not used) with its undocumented umask 0x08, as a raw perf config: the event's low byte in
// bits 0-7, the umask in bits 8-15 and the event's high nibble in bits 32-35. It counts the slots that Zen 5's
// per-core power cap withholds from a heavy vector loop at a boosted clock (#199).
#define BUSTER_X86_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull

// ---------------------------------------------------------------------------------------------------------------------
// The stream

typedef enum X86BenchmarkOperands
{
    // Register operands (or none).
    X86_BENCHMARK_REGISTERS,
    // [rsp + disp8] or [rbp + disp8]: a stack slot.
    X86_BENCHMARK_STACK,
    // [base + disp], with an 8- or 32-bit displacement.
    X86_BENCHMARK_BASE,
    // [base + index * scale + disp]
    X86_BENCHMARK_BASE_INDEX,
    // [rip + disp32], or in 16- and 32-bit modes, an absolute address: a global's.
    X86_BENCHMARK_RIP,
    // In 16- and 32-bit modes, [base + disp] in another segment (an ES override), and with the other address size (in
    // 16-bit mode, a 32-bit register's).
    X86_BENCHMARK_FAR,
    X86_BENCHMARK_OTHER_SIZE,
} X86BenchmarkOperands;

typedef struct X86BenchmarkPattern X86BenchmarkPattern;
struct X86BenchmarkPattern
{
    uint16_t form;
    uint8_t operands;
    uint16_t weight;
};

// A mix of forms and operands shaped like compiler output. The weights are per mille.
static const X86BenchmarkPattern x86_benchmark_patterns[] = {
    { X86_FORM_MOV_RM64_R64, X86_BENCHMARK_REGISTERS, 100 },
    { X86_FORM_MOV_RM32_R32, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_MOV_R64_RM64, X86_BENCHMARK_STACK, 80 },
    { X86_FORM_MOV_RM64_R64, X86_BENCHMARK_STACK, 60 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_BASE, 30 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_BASE_INDEX, 20 },
    { X86_FORM_MOV_RM32_R32, X86_BENCHMARK_BASE, 20 },
    { X86_FORM_MOV_R32_IMM32, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_MOV_R64_IMM64, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_MOV_RM32_IMM32, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_LEA_R64_M, X86_BENCHMARK_BASE, 30 },
    { X86_FORM_LEA_R64_M, X86_BENCHMARK_BASE_INDEX, 10 },
    { X86_FORM_LEA_R64_M, X86_BENCHMARK_RIP, 20 },
    { X86_FORM_ADD_RM64_IMM8, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_SUB_RM64_IMM8, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_ADD_RM64_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_SUB_RM32_R32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_AND_RM32_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_OR_RM32_R32, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_XOR_RM32_R32, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_ADD_RM32_IMM32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_CMP_RM64_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_CMP_RM32_IMM8, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_CMP_RM64_IMM8, X86_BENCHMARK_STACK, 5 },
    { X86_FORM_TEST_RM64_R64, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_TEST_RM8_R8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_JE_REL8, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_JNE_REL8, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_JL_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JG_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JB_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JAE_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JE_REL32, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_JNE_REL32, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_JMP_REL8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JMP_REL32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_CALL_REL32, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_CALL_RM64, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_RET, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_PUSH_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_POP_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_MOVZX_R32_RM8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_MOVZX_R32_RM8, X86_BENCHMARK_BASE, 10 },
    { X86_FORM_MOVSX_R64_RM8, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_MOVSXD_R64_RM32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_IMUL_R64_RM64, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_SHL_RM64_IMM8, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_SHR_RM32_IMM8, X86_BENCHMARK_REGISTERS, 6 },
    { X86_FORM_SAR_RM64_IMM8, X86_BENCHMARK_REGISTERS, 4 },
    { X86_FORM_SETE_RM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_SETL_RM8, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_CMOVE_R64_RM64, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_CMOVL_R32_RM32, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_MOVSD_X_XM64, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_MOVSD_XM64_X, X86_BENCHMARK_STACK, 8 },
    { X86_FORM_MOVSS_X_XM32, X86_BENCHMARK_BASE, 5 },
    { X86_FORM_ADDSD_X_XM64, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_MULSD_X_XM64, X86_BENCHMARK_REGISTERS, 6 },
    { X86_FORM_SUBSD_X_XM64, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_CVTSI2SD_X_RM32, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_UCOMISD_X_XM64, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_MOVAPS_X_XM, X86_BENCHMARK_REGISTERS, 6 },
    { X86_FORM_PXOR_X_XM128, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_MOVUPS_X_XM, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_MOVUPS_XM_X, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_NOP, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_ENDBR64, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_LEAVE, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_CQO, X86_BENCHMARK_REGISTERS, 1 },
    { X86_FORM_IDIV_RM64, X86_BENCHMARK_REGISTERS, 1 },
};

// A mix shaped like vectorized loops: 512-bit loads and stores, arithmetic, compares into opmasks and masked or
// compressed stores, broadcasts, some 256-bit AVX2 and BMI, and the loop's own instructions. The weights are per mille.
static const X86BenchmarkPattern x86_benchmark_vector_patterns[] = {
    { X86_FORM_VMOVDQU64_Z_ZM512, X86_BENCHMARK_BASE_INDEX, 70 },
    { X86_FORM_VMOVDQU8_Z_ZM512, X86_BENCHMARK_BASE, 40 },
    { X86_FORM_VMOVDQU64_ZM512_Z, X86_BENCHMARK_BASE_INDEX, 40 },
    { X86_FORM_VMOVDQU8_ZM512_Z, X86_BENCHMARK_BASE, 20 },
    { X86_FORM_VMOVUPS_Z_ZM512, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_VPADDD_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_VPADDD_Z_Z_M32BCST, X86_BENCHMARK_RIP, 15 },
    { X86_FORM_VPSUBQ_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPANDQ_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_VPXORQ_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_VPSHUFB_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_VPERMB_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPERMT2D_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPMULTISHIFTQB_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPSRLD_Z_ZM512_IMM8, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_VPSLLVQ_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_VPCMPEQB_K_Z_ZM512, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_VPCMPUB_K_Z_ZM512_IMM8, X86_BENCHMARK_BASE, 10 },
    { X86_FORM_VPTESTMD_K_Z_ZM512, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPSHUFBITQMB_K_Z_ZM512, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_VPCOMPRESSB_ZM512_Z, X86_BENCHMARK_BASE, 15 },
    { X86_FORM_VPBROADCASTD_Z_R32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPBROADCASTB_Z_XM8, X86_BENCHMARK_STACK, 5 },
    { X86_FORM_VPMOVM2D_Z_K, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_VADDPS_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_VADDPS_Z_Z_M32BCST, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_VMULPS_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_VFMADD231PS_Z_Z_ZM512, X86_BENCHMARK_BASE_INDEX, 30 },
    { X86_FORM_VFMADD231PS_Z_Z_Z_ER, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_VCMPPS_K_Z_ZM512_IMM8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VMAXPS_Z_Z_ZM512, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_VADDSD_X_X_XM64_EVEX, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_KMOVQ_R64_K, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_KMOVQ_K_R64, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_KORTESTQ_K_K, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_KANDQ_K_K_K, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_VMOVDQU_Y_YM256, X86_BENCHMARK_BASE_INDEX, 25 },
    { X86_FORM_VMOVDQU_YM256_Y, X86_BENCHMARK_BASE_INDEX, 15 },
    { X86_FORM_VPADDD_Y_Y_YM256, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_VPCMPEQB_Y_Y_YM256, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPMOVMSKB_R32_Y, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VPXOR_X_X_XM128, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VFMADD231PD_Y_Y_YM256, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_VBROADCASTSS_Y_XM32, X86_BENCHMARK_RIP, 5 },
    { X86_FORM_VZEROUPPER, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_SHLX_R64_RM64_R64, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_SARX_R64_RM64_R64, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_ANDN_R64_R64_RM64, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_BLSR_R64_RM64, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_TZCNT_R64_RM64, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_POPCNT_R64_RM64, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_ADD_RM64_IMM8, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_ADD_RM64_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_LEA_R64_M, X86_BENCHMARK_BASE_INDEX, 15 },
    { X86_FORM_CMP_RM64_R64, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_MOV_RM64_R64, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JNE_REL8, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_JB_REL32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_JMP_REL8, X86_BENCHMARK_REGISTERS, 5 },
};

// A mix shaped like 32-bit compiler output (i386's): arguments on the stack, so more stack loads and stores and pushes,
// globals at absolute addresses (X86_BENCHMARK_RIP's), GS for thread-local storage, and x87 besides SSE2. The weights
// are per mille.
static const X86BenchmarkPattern x86_benchmark_patterns_32[] = {
    { X86_FORM_MOV_RM32_R32, X86_BENCHMARK_REGISTERS, 100 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_STACK, 110 },
    { X86_FORM_MOV_RM32_R32, X86_BENCHMARK_STACK, 60 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_BASE, 40 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_BASE_INDEX, 20 },
    { X86_FORM_MOV_RM32_R32, X86_BENCHMARK_BASE, 20 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_RIP, 20 },
    { X86_FORM_MOV_R32_RM32 + BUSTER_X86_FORM_GS, X86_BENCHMARK_RIP, 3 },
    { X86_FORM_MOV_R32_IMM32, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_MOV_RM32_IMM32, X86_BENCHMARK_STACK, 20 },
    { X86_FORM_LEA_R32_M, X86_BENCHMARK_BASE, 30 },
    { X86_FORM_LEA_R32_M, X86_BENCHMARK_BASE_INDEX, 10 },
    { X86_FORM_ADD_RM32_IMM8, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_SUB_RM32_IMM8, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_ADD_RM32_R32, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_SUB_RM32_R32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_AND_RM32_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_OR_RM32_R32, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_XOR_RM32_R32, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_ADD_RM32_IMM32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_CMP_RM32_R32, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_CMP_RM32_IMM8, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_CMP_RM32_IMM8, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_TEST_RM32_R32, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_TEST_RM8_R8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_JE_REL8, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_JNE_REL8, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_JL_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JG_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JB_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JAE_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JE_REL32, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_JNE_REL32, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_JMP_REL8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JMP_REL32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_CALL_REL32, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_CALL_RM32, X86_BENCHMARK_BASE, 5 },
    { X86_FORM_RET, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_PUSH_R32, X86_BENCHMARK_REGISTERS, 45 },
    { X86_FORM_POP_R32, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_PUSH_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_PUSH_IMM32, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_PUSH_RM32, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_MOVZX_R32_RM8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_MOVZX_R32_RM8, X86_BENCHMARK_BASE, 10 },
    { X86_FORM_MOVSX_R32_RM8, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_MOVZX_R32_RM16, X86_BENCHMARK_BASE, 3 },
    { X86_FORM_IMUL_R32_RM32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_SHL_RM32_IMM8, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_SHR_RM32_IMM8, X86_BENCHMARK_REGISTERS, 6 },
    { X86_FORM_SAR_RM32_IMM8, X86_BENCHMARK_REGISTERS, 4 },
    { X86_FORM_SETE_RM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_SETL_RM8, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_CMOVE_R32_RM32, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_CMOVL_R32_RM32, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_FLD_M64, X86_BENCHMARK_STACK, 5 },
    { X86_FORM_FSTP_M64, X86_BENCHMARK_STACK, 5 },
    { X86_FORM_FILD_M32, X86_BENCHMARK_STACK, 2 },
    { X86_FORM_MOVSD_X_XM64, X86_BENCHMARK_STACK, 5 },
    { X86_FORM_MOVSD_XM64_X, X86_BENCHMARK_STACK, 4 },
    { X86_FORM_ADDSD_X_XM64, X86_BENCHMARK_REGISTERS, 4 },
    { X86_FORM_MULSD_X_XM64, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_CVTSI2SD_X_RM32, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_NOP, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_ENDBR32, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_LEAVE, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_CDQ, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_IDIV_RM32, X86_BENCHMARK_REGISTERS, 1 },
};

// A mix shaped like 16-bit code (real mode's): 16-bit operands and addresses ([bp + disp] for the stack, BX, SI and DI
// for pointers), globals at absolute addresses, string instructions, segment registers and ES overrides, interrupts,
// and some 32-bit operands and addresses (66 and 67). The weights are per mille.
static const X86BenchmarkPattern x86_benchmark_patterns_16[] = {
    { X86_FORM_MOV_RM16_R16, X86_BENCHMARK_REGISTERS, 100 },
    { X86_FORM_MOV_R16_RM16, X86_BENCHMARK_STACK, 90 },
    { X86_FORM_MOV_RM16_R16, X86_BENCHMARK_STACK, 60 },
    { X86_FORM_MOV_R16_RM16, X86_BENCHMARK_BASE, 50 },
    { X86_FORM_MOV_R16_RM16, X86_BENCHMARK_BASE_INDEX, 20 },
    { X86_FORM_MOV_RM16_R16, X86_BENCHMARK_BASE, 20 },
    { X86_FORM_MOV_R16_RM16, X86_BENCHMARK_RIP, 25 },
    { X86_FORM_MOV_R16_RM16, X86_BENCHMARK_FAR, 10 },
    { X86_FORM_MOV_R16_IMM16, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_MOV_RM16_IMM16, X86_BENCHMARK_STACK, 15 },
    { X86_FORM_MOV_RM8_R8, X86_BENCHMARK_BASE, 15 },
    { X86_FORM_MOV_R8_RM8, X86_BENCHMARK_BASE, 15 },
    { X86_FORM_LEA_R16_M, X86_BENCHMARK_BASE, 15 },
    { X86_FORM_ADD_RM16_IMM8, X86_BENCHMARK_REGISTERS, 30 },
    { X86_FORM_SUB_RM16_IMM8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_ADD_RM16_R16, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_SUB_RM16_R16, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_XOR_RM16_R16, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_AND_RM16_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_OR_RM16_R16, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_CMP_RM16_R16, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_CMP_RM16_IMM8, X86_BENCHMARK_REGISTERS, 25 },
    { X86_FORM_CMP_RM16_IMM8, X86_BENCHMARK_STACK, 10 },
    { X86_FORM_TEST_RM16_R16, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_TEST_RM8_R8, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_JE_REL8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JNE_REL8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JL_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JG_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JB_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JAE_REL8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JE_REL16, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JNE_REL16, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_JMP_REL8, X86_BENCHMARK_REGISTERS, 20 },
    { X86_FORM_JMP_REL16, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_CALL_REL16, X86_BENCHMARK_REGISTERS, 40 },
    { X86_FORM_RET, X86_BENCHMARK_REGISTERS, 15 },
    { X86_FORM_PUSH_R16, X86_BENCHMARK_REGISTERS, 45 },
    { X86_FORM_POP_R16, X86_BENCHMARK_REGISTERS, 35 },
    { X86_FORM_INC_R16, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_DEC_R16, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_INT_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_MOVZX_R16_RM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_IMUL_R16_RM16, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_SHL_RM16_IMM8, X86_BENCHMARK_REGISTERS, 8 },
    { X86_FORM_LEAVE, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_CWD, X86_BENCHMARK_REGISTERS, 2 },
    { X86_FORM_REP_MOVS_B, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_STOS_W, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_LODS_B, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_MOV_SREG_RM16, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_MOV_R16_SREG, X86_BENCHMARK_REGISTERS, 3 },
    { X86_FORM_LES_R16_M32, X86_BENCHMARK_STACK, 3 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_REGISTERS, 10 },
    { X86_FORM_ADD_RM32_IMM8, X86_BENCHMARK_REGISTERS, 5 },
    { X86_FORM_MOV_R32_RM32, X86_BENCHMARK_OTHER_SIZE, 5 },
};

// xorshift64*.
static uint64_t x86_benchmark_random(uint64_t* state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static uint32_t x86_benchmark_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)((x86_benchmark_random(state) >> 32) * bound >> 32);
}

// The mode of the stream and of the encoders (--mode32 and --mode16).
static X86EncoderMode x86_benchmark_mode;

// A register of the class (in a form of the family), without the high byte registers: 0 to 15, or 31 for an EVEX
// form's vector registers, or 7 for an opmask; or in 16- and 32-bit modes, 0 to 7, AH to BH for a legacy byte
// register's 4 to 7, and a segment register but CS (which MOV can't write).
static uint8_t x86_benchmark_register(uint64_t* state, uint16_t operand, uint8_t family)
{
    static const uint8_t segments[] = { 0, 2, 3, 4, 5 };
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    const bool legacy = x86_mode_is_legacy(x86_benchmark_mode);
    const uint32_t count = class == BUSTER_X86_CLASS_OPMASK || legacy                                                ? 8
                           : class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX ? 32
                                                                                                                          : 16;
    const uint8_t number = (uint8_t)x86_benchmark_below(state, count);
    const bool high_byte = legacy && class == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY && number >= 4;
    const uint8_t result = (uint8_t)(number | (high_byte ? BUSTER_X86_REGISTER_HIGH_BYTE : 0));
    return legacy && class == BUSTER_X86_CLASS_SEGMENT ? segments[number % BUSTER_ARRAY_LENGTH(segments)] : result;
}

// A general register for a memory operand's base or index: 0 to 7 in 32-bit mode, and BX, BP, SI or DI in 16-bit mode.
static uint8_t x86_benchmark_address_register(uint64_t* state)
{
    static const uint8_t registers16[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_RSI, X86_REGISTER_RDI };
    return x86_benchmark_mode == X86_ENCODER_MODE_16   ? registers16[x86_benchmark_below(state, 4)]
           : x86_benchmark_mode == X86_ENCODER_MODE_32 ? (uint8_t)x86_benchmark_below(state, 8)
                                                       : (uint8_t)x86_benchmark_below(state, 16);
}

// Whether the mode's encoders can encode the instruction.
static bool x86_benchmark_is_valid(const X86Instruction* instruction)
{
    return x86_benchmark_mode == X86_ENCODER_MODE_32   ? x86_instruction_is_valid_32(instruction)
           : x86_benchmark_mode == X86_ENCODER_MODE_16 ? x86_instruction_is_valid_16(instruction)
                                                       : x86_instruction_is_valid(instruction);
}

// The register operands the form has besides rm: reg and vvvv.
static void x86_benchmark_registers(uint64_t* state, const X86FormDescription* form, X86Instruction* instruction)
{
    if (form->encoding == BUSTER_X86_ENCODING_R)
    {
        instruction->reg = x86_benchmark_register(state, form->reg, form->family);
    }
    if (form->vvvv != BUSTER_X86_OPERAND_NONE)
    {
        instruction->vvvv = x86_benchmark_register(state, form->vvvv, form->family);
    }
}

// A third of the time, an opmask, if the form can have one (and zeroing, half of those, when the destination is a
// register).
static void x86_benchmark_decorations(uint64_t* state, const X86FormDescription* form, X86Instruction* instruction)
{
    if ((form->evex & BUSTER_X86_EVEX_MASK) && x86_benchmark_below(state, 3) == 0)
    {
        const bool memory_destination = (instruction->mode & BUSTER_X86_MODE_MEMORY) && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM);
        instruction->decorations = (uint8_t)(1 + x86_benchmark_below(state, 7));
        if ((form->evex & BUSTER_X86_EVEX_ZEROING) && !memory_destination && x86_benchmark_below(state, 2) == 0)
        {
            instruction->decorations |= BUSTER_X86_DECORATION_ZEROING;
        }
    }
}

// An immediate for the form: small, as most are, or an /is4 register.
static int32_t x86_benchmark_immediate(uint64_t* state, const X86FormDescription* form)
{
    int32_t result = 0;
    if (x86_form_has_is4(form))
    {
        result = (int32_t)x86_benchmark_below(state, 16) << 4;
    }
    else if (form->immediate_bytes == 1)
    {
        result = (int32_t)x86_benchmark_below(state, 128);
    }
    else if (form->immediate_bytes >= 2)
    {
        result = (int32_t)x86_benchmark_below(state, form->immediate_bytes == 2 ? 30000 : 100000);
    }
    return result;
}

static X86Instruction pattern_instruction(uint64_t* state, const X86BenchmarkPattern* pattern)
{
    const X86FormDescription* form = x86_form_description(pattern->form);
    X86Instruction result = { .form = pattern->form };
    x86_benchmark_registers(state, form, &result);
    if (form->rm != BUSTER_X86_OPERAND_NONE)
    {
        switch (pattern->operands)
        {
            case X86_BENCHMARK_REGISTERS:
                result.rm = x86_benchmark_register(state, form->rm, form->family);
                break;
            case X86_BENCHMARK_STACK:
                // In 16-bit mode, [bp + disp] alone, as SP can't be a base there.
                result.mode = BUSTER_X86_MEMORY_BASE;
                result.rm = x86_benchmark_below(state, 2) || x86_benchmark_mode == X86_ENCODER_MODE_16 ? X86_REGISTER_RBP : X86_REGISTER_RSP;
                result.displacement = result.rm == X86_REGISTER_RBP ? -8 * (int32_t)(1 + x86_benchmark_below(state, 15)) : 8 * (int32_t)x86_benchmark_below(state, 15);
                break;
            case X86_BENCHMARK_BASE:
                // Small displacements are in units of an EVEX form's N, as they'd be from arrays of its elements.
                result.mode = BUSTER_X86_MEMORY_BASE;
                result.rm = x86_benchmark_address_register(state);
                result.displacement = x86_benchmark_below(state, 4) ? (int32_t)(x86_benchmark_below(state, 128) * form->disp8_scale)
                                                                    : (int32_t)x86_benchmark_below(state, 1u << (x86_benchmark_mode == X86_ENCODER_MODE_16 ? 16 : 20));
                break;
            case X86_BENCHMARK_BASE_INDEX:
                // In 16-bit mode, BX or BP and SI or DI, without a scale.
                result.mode = BUSTER_X86_MEMORY_BASE_INDEX(x86_benchmark_mode == X86_ENCODER_MODE_16 ? 0 : x86_benchmark_below(state, 4));
                result.rm = x86_benchmark_address_register(state);
                result.index = x86_benchmark_address_register(state);
                result.index = result.index == X86_REGISTER_RSP ? X86_REGISTER_RAX : result.index;
                if (x86_benchmark_mode == X86_ENCODER_MODE_16)
                {
                    result.rm = result.rm == X86_REGISTER_RSI ? X86_REGISTER_RBX : result.rm == X86_REGISTER_RDI ? X86_REGISTER_RBP : result.rm;
                    result.index = result.index == X86_REGISTER_RBX ? X86_REGISTER_RSI : result.index == X86_REGISTER_RBP ? X86_REGISTER_RDI : result.index;
                }
                result.displacement = (int32_t)(x86_benchmark_below(state, 64) * form->disp8_scale);
                break;
            case X86_BENCHMARK_FAR:
                result.mode = BUSTER_X86_MEMORY_BASE;
                result.rm = x86_benchmark_address_register(state);
                result.displacement = (int32_t)x86_benchmark_below(state, 256);
                result.decorations = BUSTER_X86_SEGMENT_OVERRIDE(0);
                break;
            case X86_BENCHMARK_OTHER_SIZE:
                // In 16-bit mode, a 32-bit register's [base + disp], and in 32-bit mode, [bx + disp].
                result.mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE;
                result.rm = x86_benchmark_mode == X86_ENCODER_MODE_16 ? (uint8_t)x86_benchmark_below(state, 8) : X86_REGISTER_RBX;
                result.displacement = (int32_t)x86_benchmark_below(state, 1 << 12);
                break;
            default:
                // In 16- and 32-bit modes, an absolute address.
                result.mode = x86_mode_is_legacy(x86_benchmark_mode) ? BUSTER_X86_MEMORY_ABSOLUTE : BUSTER_X86_MEMORY_RIP;
                result.displacement = (int32_t)x86_benchmark_below(state, 1u << (x86_benchmark_mode == X86_ENCODER_MODE_16 ? 16 : 24));
                break;
        }
    }
    x86_benchmark_decorations(state, form, &result);
    result.immediate = x86_benchmark_immediate(state, form);
    if (form->immediate_bytes == 8)
    {
        result.displacement = (int32_t)x86_benchmark_below(state, 1 << 16);
    }
    if (form->relative_bytes == 1)
    {
        result.displacement = (int32_t)x86_benchmark_below(state, 256) - 128;
    }
    else if (form->relative_bytes == 2)
    {
        result.displacement = (int32_t)x86_benchmark_below(state, 1 << 15) - (1 << 14);
    }
    else if (form->relative_bytes == 4)
    {
        result.displacement = (int32_t)x86_benchmark_below(state, 1 << 20) - (1 << 19);
    }
    return result;
}

// A random valid instruction of any form, operands of every kind (as the tests make them).
static X86Instruction uniform_instruction(uint64_t* state)
{
    X86Instruction result;
    bool valid = false;
    while (!valid)
    {
        const uint16_t form_index = (uint16_t)(1 + x86_benchmark_below(state, X86_FORM_COUNT - 1));
        const X86FormDescription* form = &x86_form_descriptions[form_index];
        result = (X86Instruction){ .form = form_index };
        x86_benchmark_registers(state, form, &result);
        if (form->rm != BUSTER_X86_OPERAND_NONE)
        {
            const bool can_register = (form->rm & BUSTER_X86_OPERAND_REGISTER) != 0;
            if (can_register && ((form->rm & BUSTER_X86_OPERAND_MEMORY) == 0 || x86_benchmark_below(state, 2)))
            {
                result.rm = x86_benchmark_register(state, form->rm, form->family);
            }
            else
            {
                static const uint8_t modes[] = { BUSTER_X86_MEMORY_BASE, BUSTER_X86_MEMORY_BASE_INDEX(2), BUSTER_X86_MEMORY_INDEX(3), BUSTER_X86_MEMORY_ABSOLUTE,
                                                 BUSTER_X86_MEMORY_RIP };
                result.mode = modes[x86_benchmark_below(state, BUSTER_ARRAY_LENGTH(modes))];
                result.rm = (result.mode & BUSTER_X86_MODE_BASE) ? x86_benchmark_address_register(state) : 0;
                result.index = (result.mode & BUSTER_X86_MODE_INDEX) ? (uint8_t)(x86_benchmark_below(state, 2) ? x86_benchmark_below(state, 4) : 5 + x86_benchmark_below(state, 11)) : 0;
                result.displacement = x86_benchmark_below(state, 2) ? (int32_t)x86_benchmark_below(state, 256) - 128 : (int32_t)(uint32_t)x86_benchmark_random(state);
            }
        }
        x86_benchmark_decorations(state, form, &result);
        result.immediate = x86_benchmark_immediate(state, form);
        if (form->immediate_bytes == 8)
        {
            result.displacement = (int32_t)(uint32_t)x86_benchmark_random(state);
        }
        if (form->relative_bytes == 1)
        {
            result.displacement = (int32_t)x86_benchmark_below(state, 256) - 128;
        }
        else if (form->relative_bytes == 4)
        {
            result.displacement = (int32_t)(uint32_t)x86_benchmark_random(state);
        }
        valid = x86_benchmark_is_valid(&result);
    }
    return result;
}

// Fills `instructions` with the stream, from the patterns or with `uniform`, uniformly. Returns false if a pattern
// makes an invalid instruction (a broken table).
static bool make_stream(X86Instruction* instructions, uint64_t count, const X86BenchmarkPattern* patterns, uint64_t pattern_count, bool uniform, uint64_t seed)
{
    uint32_t total_weight = 0;
    for (uint64_t p = 0; p < pattern_count; p += 1)
    {
        total_weight += patterns[p].weight;
    }
    uint64_t state = seed * BUSTER_X86_BENCHMARK_GOLDEN_RATIO + 1;
    bool result = true;
    for (uint64_t i = 0; i < count; i += 1)
    {
        if (uniform)
        {
            instructions[i] = uniform_instruction(&state);
        }
        else
        {
            uint32_t pick = x86_benchmark_below(&state, total_weight);
            uint64_t p = 0;
            while (pick >= patterns[p].weight)
            {
                pick -= patterns[p].weight;
                p += 1;
            }
            instructions[i] = pattern_instruction(&state, &patterns[p]);
            result = result && x86_benchmark_is_valid(&instructions[i]);
        }
    }
    return result;
}

// Marks the instructions a code generator would have fixups for: calls (to other functions; branches inside a function
// are resolved before encoding) and RIP-relative operands (globals and constants, at absolute addresses in 16- and
// 32-bit modes), and lists their indices in `marked`. Returns how many.
static uint64_t x86_benchmark_mark(X86Instruction* instructions, uint64_t count, uint32_t* marked)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint8_t parts = instructions[i].mode & (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX);
        const bool global = (instructions[i].mode & BUSTER_X86_MODE_RIP) != 0 || (x86_mode_is_legacy(x86_benchmark_mode) && parts == BUSTER_X86_MODE_MEMORY);
        if (instructions[i].form == X86_FORM_CALL_REL32 || instructions[i].form == X86_FORM_CALL_REL16 || global)
        {
            instructions[i].form |= BUSTER_X86_FORM_FIXUP;
            marked[result] = (uint32_t)i;
            result += 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The implementations

// The scalar encoder's loop without its checks: what one instruction at a time costs (in the mode, a constant), with
// encode_one inlined (flatten), as clang does for one call of it alone.
BUSTER_INLINE __attribute__((flatten)) int64_t scalar_loop(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t* fixup_ends,
                                                           X86EncoderMode encoder)
{
    uint64_t length = 0;
    uint64_t marked = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        if (offsets != 0)
        {
            offsets[i] = (uint32_t)length;
        }
        length += encode_one(&instructions[i], output + length, encoder);
        if (fixup_ends != 0 && (instructions[i].form & BUSTER_X86_FORM_FIXUP))
        {
            fixup_ends[marked] = (uint32_t)length;
            marked += 1;
        }
    }
    return (int64_t)length;
}

__attribute__((noinline)) static int64_t encode_scalar_loop(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets,
                                                            uint32_t* fixup_ends)
{
    return scalar_loop(instructions, count, output, offsets, fixup_ends, X86_ENCODER_MODE_64);
}

__attribute__((noinline)) static int64_t encode_scalar_loop_32(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets,
                                                               uint32_t* fixup_ends)
{
    return scalar_loop(instructions, count, output, offsets, fixup_ends, X86_ENCODER_MODE_32);
}

__attribute__((noinline)) static int64_t encode_scalar_loop_16(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets,
                                                               uint32_t* fixup_ends)
{
    return scalar_loop(instructions, count, output, offsets, fixup_ends, X86_ENCODER_MODE_16);
}

// The offsets encode_offsets_then_pick stores when its caller wants none (room for the stream's).
static uint32_t* x86_benchmark_starts;
// What a code generator knows about the instructions of each call encode_offsets_then_pick makes: which are marked (their
// indices, in order). encode_stream sets them before each call.
static const uint32_t* x86_benchmark_marked;
static uint64_t x86_benchmark_marked_count;

// The other way to have fixup ends: x86_encode_64 with every instruction's offset, then the caller picks the ends of the
// marked instructions (the next instruction's start) from its list of them.
__attribute__((noinline)) static int64_t encode_offsets_then_pick(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets,
                                                                  uint32_t* fixup_ends)
{
    uint32_t* starts = offsets != 0 ? offsets : x86_benchmark_starts;
    const int64_t result = x86_encode_64(instructions, count, output, starts, 0);
    for (uint64_t k = 0; fixup_ends != 0 && result >= 0 && k < x86_benchmark_marked_count; k += 1)
    {
        const uint32_t i = x86_benchmark_marked[k];
        fixup_ends[k] = i + 1 < count ? starts[i + 1] : (uint32_t)result;
    }
    return result;
}

typedef int64_t X86BenchmarkFunction(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t* fixup_ends);

typedef struct X86BenchmarkImplementation X86BenchmarkImplementation;
struct X86BenchmarkImplementation
{
    const char* name;
    X86BenchmarkFunction* encode;
};

// The scalar loop first: the others are checked against it.
static const X86BenchmarkImplementation x86_benchmark_implementations[] = {
    { "scalar", encode_scalar_loop },
    { "x86_encode_4", x86_encode_4 },
    { "x86_encode_8", x86_encode_8 },
    { "x86_encode_16", x86_encode_16 },
    { "x86_encode_32", x86_encode_32 },
    { "x86_encode_64", x86_encode_64 },
    { "x86_encode_16_forms_from_records", x86_encode_16_forms_from_records },
    { "x86_encode_16_masked_loads", x86_encode_16_masked_loads },
    { "x86_encode_16_masked_stores", x86_encode_16_masked_stores },
    { "x86_encode_16_compress_stores", x86_encode_16_compress_stores },
    { "x86_encode_64_fixup_compress_stores", x86_encode_64_fixup_compress_stores },
    { "x86_encode_64_offsets_then_pick", encode_offsets_then_pick },
    { "x86_encode_64_apx", x86_encode_64_apx },
    { "x86_encode_16_apx", x86_encode_16_apx },
};
#define BUSTER_X86_BENCHMARK_IMPLEMENTATIONS BUSTER_ARRAY_LENGTH(x86_benchmark_implementations)

// The same for 32-bit and 16-bit modes (--mode32 and --mode16): the mode's scalar loop and its loops at 64 and 16 per
// iteration (x86_encode_mode32's or x86_encode_mode16's).
static const X86BenchmarkImplementation x86_benchmark_implementations_32[] = {
    { "scalar", encode_scalar_loop_32 },
    { "x86_encode_64_mode32", x86_encode_64_mode32 },
    { "x86_encode_16_mode32", x86_encode_16_mode32 },
};

static const X86BenchmarkImplementation x86_benchmark_implementations_16[] = {
    { "scalar", encode_scalar_loop_16 },
    { "x86_encode_64_mode16", x86_encode_64_mode16 },
    { "x86_encode_16_mode16", x86_encode_16_mode16 },
};

// ---------------------------------------------------------------------------------------------------------------------
// Timing

typedef struct X86BenchmarkSample X86BenchmarkSample;
struct X86BenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t throttle;
};

// The perf counters' descriptors, each -1 if it isn't open.
typedef struct X86BenchmarkCounters X86BenchmarkCounters;
struct X86BenchmarkCounters
{
    int cycles;
    int instructions;
    int throttle;
};

// A counter of this thread's user-space events of the type (PERF_TYPE_HARDWARE or PERF_TYPE_RAW), or -1 if
// perf_event_open fails.
static int x86_benchmark_open_counter(uint32_t type, uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = type;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t x86_benchmark_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static uint64_t x86_benchmark_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static X86BenchmarkSample x86_benchmark_now(const X86BenchmarkCounters* counters)
{
    X86BenchmarkSample result = {
        .nanoseconds = x86_benchmark_nanoseconds(),
        .cycles = x86_benchmark_read_counter(counters->cycles),
        .instructions = x86_benchmark_read_counter(counters->instructions),
        .throttle = x86_benchmark_read_counter(counters->throttle),
    };
    return result;
}

static void x86_benchmark_sort(uint64_t* values, uint64_t count)
{
    for (uint64_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint64_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

// Encodes the stream in calls of `section` instructions, appending to `output`, each call's fixup ends (from its own
// output's start) after the previous calls'. `marked` lists the marked instructions (by index in their call's
// instructions) and section_marks[s] how many of call s's instructions are marked. Returns the number of bytes, or a
// negative error. Without fixup ends, a call costs what it did before they existed, so that short calls compare with
// older measurements.
static int64_t encode_stream(const X86BenchmarkImplementation* implementation, const X86Instruction* instructions, uint64_t count, uint64_t section, uint8_t* output,
                             uint32_t* offsets, uint32_t* fixup_ends, const uint32_t* marked, const uint32_t* section_marks)
{
    int64_t result = 0;
    uint64_t marks = 0;
    uint64_t s = 0;
    for (uint64_t start = 0; result >= 0 && start < count; start += section)
    {
        const uint64_t n = count - start < section ? count - start : section;
        int64_t length;
        if (fixup_ends != 0)
        {
            x86_benchmark_marked = marked + marks;
            x86_benchmark_marked_count = section_marks[s];
            length = implementation->encode(instructions + start, n, output + result, offsets != 0 ? offsets + start : 0, fixup_ends + marks);
            marks += section_marks[s];
            s += 1;
        }
        else
        {
            length = implementation->encode(instructions + start, n, output + result, offsets != 0 ? offsets + start : 0, 0);
        }
        result = length < 0 ? length : result + length;
    }
    return result;
}

// --after-scalar: the scalar loop (`scalar`, implementation 0) on the stream's first instructions for `milliseconds`,
// before a timed pass. A compiler runs its vector phases in short bursts after lighter ones, which let the clock boost
// (from about 5.31 to 5.52 GHz on the 9700X, over 30 to 300 ms), and at that clock a heavy vector loop runs under the
// core's power cap until the clock comes down, 5 to 20 ms later (#199; docs/x86-64-encoder.md, "Zen 5's costs,
// measured"). Back-to-back passes miss that state.
static void x86_benchmark_after_scalar(const X86BenchmarkImplementation* scalar, const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t milliseconds)
{
    const uint64_t n = count < BUSTER_X86_BENCHMARK_AFTER_SCALAR_INSTRUCTIONS ? count : BUSTER_X86_BENCHMARK_AFTER_SCALAR_INSTRUCTIONS;
    const uint64_t end = x86_benchmark_nanoseconds() + milliseconds * 1000000;
    while (x86_benchmark_nanoseconds() < end)
    {
        scalar->encode(instructions, n, output, 0, 0);
    }
}

static bool x86_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0;
    *value = result ? (uint64_t)parsed : 0;
    return result;
}

int run_x86_encoder_benchmark(int argc, char** argv)
{
    uint64_t count = BUSTER_X86_BENCHMARK_DEFAULT_COUNT;
    uint64_t section = 0;
    uint64_t seed = 1;
    bool uniform = false;
    bool vector = false;
    X86EncoderMode mode = X86_ENCODER_MODE_64;
    bool with_offsets = false;
    bool with_fixups = false;
    uint64_t after_scalar = 0;
    const char* only = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            valid = valid && x86_benchmark_parse(argv[i + 1], &count);
            i += 1;
        }
        else if (strcmp(argv[i], "--section") == 0 && i + 1 < argc)
        {
            valid = valid && x86_benchmark_parse(argv[i + 1], &section);
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && x86_benchmark_parse(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--uniform") == 0)
        {
            uniform = true;
        }
        else if (strcmp(argv[i], "--vector") == 0)
        {
            vector = true;
        }
        else if (strcmp(argv[i], "--mode32") == 0 || strcmp(argv[i], "--mode16") == 0)
        {
            valid = valid && mode == X86_ENCODER_MODE_64;
            mode = strcmp(argv[i], "--mode32") == 0 ? X86_ENCODER_MODE_32 : X86_ENCODER_MODE_16;
        }
        else if (strcmp(argv[i], "--offsets") == 0)
        {
            with_offsets = true;
        }
        else if (strcmp(argv[i], "--fixups") == 0)
        {
            with_fixups = true;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            valid = valid && x86_benchmark_parse(argv[i + 1], &after_scalar) && after_scalar <= BUSTER_X86_BENCHMARK_AFTER_SCALAR_MAX_MS;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[i + 1];
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    section = section == 0 ? count : section;
    x86_benchmark_mode = mode;
    const X86BenchmarkImplementation* implementations = mode == X86_ENCODER_MODE_32   ? x86_benchmark_implementations_32
                                                        : mode == X86_ENCODER_MODE_16 ? x86_benchmark_implementations_16
                                                                                      : x86_benchmark_implementations;
    const uint64_t implementation_count = mode == X86_ENCODER_MODE_32   ? BUSTER_ARRAY_LENGTH(x86_benchmark_implementations_32)
                                          : mode == X86_ENCODER_MODE_16 ? BUSTER_ARRAY_LENGTH(x86_benchmark_implementations_16)
                                                                        : BUSTER_X86_BENCHMARK_IMPLEMENTATIONS;
    const X86BenchmarkPattern* patterns = mode == X86_ENCODER_MODE_32 ? x86_benchmark_patterns_32
                                          : mode == X86_ENCODER_MODE_16 ? x86_benchmark_patterns_16
                                          : vector                      ? x86_benchmark_vector_patterns
                                                                        : x86_benchmark_patterns;
    const uint64_t pattern_count = mode == X86_ENCODER_MODE_32   ? BUSTER_ARRAY_LENGTH(x86_benchmark_patterns_32)
                                   : mode == X86_ENCODER_MODE_16 ? BUSTER_ARRAY_LENGTH(x86_benchmark_patterns_16)
                                   : vector                      ? BUSTER_ARRAY_LENGTH(x86_benchmark_vector_patterns)
                                                                 : BUSTER_ARRAY_LENGTH(x86_benchmark_patterns);

    int result = 1;
    X86Instruction* instructions = 0;
    uint32_t* marked_indices = 0;
    uint32_t* section_marks = 0;
    uint8_t* outputs[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS] = { 0 };
    uint32_t* offsets[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS] = { 0 };
    uint32_t* fixup_ends[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS] = { 0 };
    if (!valid || count == 0 || count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS || (uniform && vector) || (vector && mode != X86_ENCODER_MODE_64))
    {
        fprintf(stderr, "usage: %s [--count N] [--section N] [--uniform | --vector] [--mode32 | --mode16] [--offsets] [--fixups] [--after-scalar MS] [--seed N] [--only NAME]\n",
                argv[0]);
    }
    else if (!x86_encoder_initialize() || !x86_encoder_initialize_apx() || !x86_encoder_initialize_legacy())
    {
        fprintf(stderr, "a form doesn't fit in a template\n");
    }
    else
    {
        const uint64_t sections = (count + section - 1) / section;
        bool allocated = (instructions = aligned_alloc(64, (count * sizeof(X86Instruction) + 63) / 64 * 64)) != 0;
        allocated = allocated && (marked_indices = calloc(count, sizeof(uint32_t))) != 0;
        allocated = allocated && (section_marks = calloc(sections, sizeof(uint32_t))) != 0;
        allocated = allocated && (x86_benchmark_starts = aligned_alloc(64, (count * sizeof(uint32_t) + 63) / 64 * 64)) != 0;
        for (uint64_t i = 0; allocated && i < implementation_count; i += 1)
        {
            const uint64_t output_bytes = (BUSTER_X86_ENCODER_CAPACITY(count) + 63) / 64 * 64;
            outputs[i] = aligned_alloc(64, output_bytes);
            offsets[i] = aligned_alloc(64, (count * sizeof(uint32_t) + 63) / 64 * 64);
            fixup_ends[i] = aligned_alloc(64, (BUSTER_X86_ENCODER_FIXUP_CAPACITY(count) * sizeof(uint32_t) + 63) / 64 * 64);
            allocated = outputs[i] != 0 && offsets[i] != 0 && fixup_ends[i] != 0;
        }

        if (!allocated)
        {
            fprintf(stderr, "out of memory\n");
        }
        else if (!make_stream(instructions, count, patterns, pattern_count, uniform, seed))
        {
            fprintf(stderr, "a benchmark pattern makes an invalid instruction\n");
        }
        else
        {
            // The marks, the list of them (by index in each call's instructions) and how many each call has.
            uint64_t marked = 0;
            for (uint64_t s = 0; with_fixups && s < sections; s += 1)
            {
                const uint64_t start = s * section;
                section_marks[s] = (uint32_t)x86_benchmark_mark(instructions + start, count - start < section ? count - start : section, marked_indices + marked);
                marked += section_marks[s];
            }

            // Check every implementation against the scalar loop (with offsets and fixup ends, whether or not the timed
            // runs have them).
            int64_t lengths[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS];
            uint64_t different = 0;
            for (uint64_t i = 0; i < implementation_count; i += 1)
            {
                lengths[i] = encode_stream(&implementations[i], instructions, count, section, outputs[i], offsets[i], fixup_ends[i], marked_indices, section_marks);
                const bool same = lengths[i] == lengths[0] && lengths[i] >= 0 && memcmp(outputs[i], outputs[0], (uint64_t)lengths[0]) == 0 &&
                                  memcmp(offsets[i], offsets[0], count * sizeof(uint32_t)) == 0 &&
                                  memcmp(fixup_ends[i], fixup_ends[0], marked * sizeof(uint32_t)) == 0;
                if (!same)
                {
                    fprintf(stderr, "%s's output differs from the scalar loop's\n", implementations[i].name);
                    different += 1;
                }
            }
            // The implementations to time: all of them, or the one --only names.
            bool selected[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS];
            for (uint64_t i = 0; i < implementation_count; i += 1)
            {
                selected[i] = only == 0 || strcmp(only, implementations[i].name) == 0;
            }
            const uint64_t repeat = (BUSTER_X86_BENCHMARK_PASS_INSTRUCTIONS + count - 1) / count;
            const X86BenchmarkCounters counters = {
                .cycles = x86_benchmark_open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
                .instructions = x86_benchmark_open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
                .throttle = x86_benchmark_open_counter(PERF_TYPE_RAW, BUSTER_X86_BENCHMARK_THROTTLE_EVENT),
            };
            if (counters.cycles < 0 || counters.instructions < 0)
            {
                fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
            }
            if (counters.throttle < 0)
            {
                fprintf(stderr, "perf_event_open failed for PMCx1A0 umask 0x08 (raw event 0x%llx): the throttle reads as 0\n", BUSTER_X86_BENCHMARK_THROTTLE_EVENT);
            }

            static X86BenchmarkSample samples[BUSTER_X86_BENCHMARK_IMPLEMENTATIONS][BUSTER_X86_BENCHMARK_PASSES];
            for (uint64_t pass = 0; pass <= BUSTER_X86_BENCHMARK_PASSES; pass += 1)
            {
                for (uint64_t i = 0; i < implementation_count; i += 1)
                {
                    if (selected[i])
                    {
                        // Not before the scalar loop's own passes, which are that phase already.
                        if (after_scalar != 0 && i != 0)
                        {
                            x86_benchmark_after_scalar(&implementations[0], instructions, count, outputs[0], after_scalar);
                        }
                        const X86BenchmarkSample before = x86_benchmark_now(&counters);
                        for (uint64_t r = 0; r < repeat; r += 1)
                        {
                            encode_stream(&implementations[i], instructions, count, section, outputs[i], with_offsets ? offsets[i] : 0,
                                          with_fixups ? fixup_ends[i] : 0, marked_indices, section_marks);
                        }
                        const X86BenchmarkSample after = x86_benchmark_now(&counters);
                        if (pass > 0)
                        {
                            samples[i][pass - 1] = (X86BenchmarkSample){
                                .nanoseconds = after.nanoseconds - before.nanoseconds,
                                .cycles = after.cycles - before.cycles,
                                .instructions = after.instructions - before.instructions,
                                .throttle = after.throttle - before.throttle,
                            };
                        }
                    }
                }
            }

            const double encoded = (double)count * (double)repeat;
            char fixups_text[64] = "";
            if (with_fixups)
            {
                snprintf(fixups_text, sizeof(fixups_text), ", with fixup ends (%lu marked)", (unsigned long)marked);
            }
            char after_text[64] = "";
            if (after_scalar != 0)
            {
                snprintf(after_text, sizeof(after_text), ", each after %lu ms of the scalar loop", (unsigned long)after_scalar);
            }
            const char* mode_words = mode == X86_ENCODER_MODE_32 ? " in 32-bit mode" : mode == X86_ENCODER_MODE_16 ? " in 16-bit mode" : "";
            printf("%lu %s instructions%s, %lld bytes (%.2f per instruction), in calls of %lu%s%s; %d interleaved passes of %lu runs%s, medians (cycles also "
                   "min-max):\n",
                   (unsigned long)count, uniform ? "uniform" : vector ? "vector" : "mixed", mode_words, (long long)lengths[0], (double)lengths[0] / (double)count,
                   (unsigned long)section, with_offsets ? ", with offsets" : "", fixups_text, BUSTER_X86_BENCHMARK_PASSES, (unsigned long)repeat, after_text);
            for (uint64_t i = 0; i < implementation_count; i += 1)
            {
                if (selected[i])
                {
                    uint64_t nanoseconds[BUSTER_X86_BENCHMARK_PASSES];
                    uint64_t cycles[BUSTER_X86_BENCHMARK_PASSES];
                    uint64_t retired[BUSTER_X86_BENCHMARK_PASSES];
                    uint64_t throttle[BUSTER_X86_BENCHMARK_PASSES];
                    for (uint64_t pass = 0; pass < BUSTER_X86_BENCHMARK_PASSES; pass += 1)
                    {
                        nanoseconds[pass] = samples[i][pass].nanoseconds;
                        cycles[pass] = samples[i][pass].cycles;
                        retired[pass] = samples[i][pass].instructions;
                        throttle[pass] = samples[i][pass].throttle;
                    }
                    x86_benchmark_sort(nanoseconds, BUSTER_X86_BENCHMARK_PASSES);
                    x86_benchmark_sort(cycles, BUSTER_X86_BENCHMARK_PASSES);
                    x86_benchmark_sort(retired, BUSTER_X86_BENCHMARK_PASSES);
                    x86_benchmark_sort(throttle, BUSTER_X86_BENCHMARK_PASSES);
                    const double median_nanoseconds = (double)nanoseconds[BUSTER_X86_BENCHMARK_PASSES / 2];
                    const double median_cycles = (double)cycles[BUSTER_X86_BENCHMARK_PASSES / 2];
                    const double median_retired = (double)retired[BUSTER_X86_BENCHMARK_PASSES / 2];
                    const double median_throttle = (double)throttle[BUSTER_X86_BENCHMARK_PASSES / 2];
                    printf("  %-32s %7.3f ns  %8.1f M/s  %6.2f GB/s  %6.2f cycles [%.2f-%.2f]  %6.2f instructions  IPC %.2f  throttle %5.2f\n", implementations[i].name,
                           median_nanoseconds / encoded, encoded / median_nanoseconds * 1e3, (double)lengths[0] * (double)repeat / median_nanoseconds,
                           median_cycles / encoded, (double)cycles[0] / encoded, (double)cycles[BUSTER_X86_BENCHMARK_PASSES - 1] / encoded, median_retired / encoded,
                           median_cycles != 0 ? median_retired / median_cycles : 0.0, median_throttle / encoded);
                }
            }
            printf("  (per encoded instruction; GB/s of machine code; throttle: the dispatch slots the power cap takes, PMCx1A0 umask 0x08)\n");
            if (counters.cycles >= 0)
            {
                close(counters.cycles);
            }
            if (counters.instructions >= 0)
            {
                close(counters.instructions);
            }
            if (counters.throttle >= 0)
            {
                close(counters.throttle);
            }
            result = different == 0 ? 0 : 1;
        }
    }
    free(instructions);
    free(marked_indices);
    free(section_marks);
    free(x86_benchmark_starts);
    x86_benchmark_starts = 0;
    for (uint64_t i = 0; i < BUSTER_X86_BENCHMARK_IMPLEMENTATIONS; i += 1)
    {
        free(outputs[i]);
        free(offsets[i]);
        free(fixup_ends[i]);
    }
    return result;
}
