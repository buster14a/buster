// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-x86-intrinsics (#136): the compiler's code of the headers' x86 intrinsics against clang -O0's, for the builtins
// whose instructions neither the tests' hosts nor Zen 5 runs (XOP's, AVX-NE-CONVERT's, AVX10.2's, SHA512's, SM3's,
// SM4's, MOVRS's, AVX512-BMM's, the extensions' system instructions'), which no native run can compare, and for
// controls that run. A source takes every case's address, as --lower-all of main.c lowers the headers' definitions:
// clang -O0 compiles its out-of-line copy of each always_inline definition, and the compiler, by both paths into the
// same bytes, its own. Each object's functions are decoded (x86_64_disassembler.c) and followed from their entry, every
// register's, opmask's and stack byte's value as x86_semantics.c's effects say the instructions read and write them:
// through moves, stack slots and the calls into the object's other functions (the compiler's code calls the definitions
// that clang inlines). A value is an expression of the arguments' bytes, with a node for each instruction that isn't a
// move (its form, immediate and operands' values; a masked one as select(mask, node, else)), so the code of each case
// must give the same expression for its result (its return register's bytes for the intrinsic's type), for its stores
// through pointers and for its effects (an instruction that writes no register, memory or flag, and a prefetch's or a
// cache line's hint of its address, in order among the stores), by both compilers.
//
// Layout: the cases (x86_intrinsic_cases) and the probe's source; the objects (X86IntrinsicObject: sections, symbols
// and relocations); the trace's values (X86IntrinsicByte, X86IntrinsicNode, x86_intrinsic_node, x86_intrinsic_value);
// operands (X86IntrinsicOperand, x86_intrinsic_operands), memory (x86_intrinsic_address) and the step
// (x86_intrinsic_step); the walk over calls and jumps (x86_intrinsic_trace); and the command
// (run_x86_intrinsic_tests).

#include <elf.h>

// An intrinsic's result: a vector of its bytes in XMM0's, or an integer of its bytes in RAX's, or none.
typedef enum X86IntrinsicReturn
{
    X86_INTRINSIC_RETURN_VECTOR16,
    X86_INTRINSIC_RETURN_VECTOR32,
    X86_INTRINSIC_RETURN_VECTOR64,
    X86_INTRINSIC_RETURN_INTEGER1,
    X86_INTRINSIC_RETURN_INTEGER2,
    X86_INTRINSIC_RETURN_INTEGER4,
    X86_INTRINSIC_RETURN_INTEGER8,
    X86_INTRINSIC_RETURN_NONE,
} X86IntrinsicReturn;

typedef struct X86IntrinsicCase X86IntrinsicCase;
struct X86IntrinsicCase
{
    const char* name;
    uint8_t result;
    // Whether clang's code branches the other way from what the intrinsic's documentation says, so that its paths'
    // conditions compare negated (x86_intrinsic_trace's `invert`): Key Locker's aesenc and aesdec intrinsics.
    bool inverted;
};

#define BUSTER_X86_INTRINSIC_X(name) { #name, X86_INTRINSIC_RETURN_VECTOR16 },
#define BUSTER_X86_INTRINSIC_Y(name) { #name, X86_INTRINSIC_RETURN_VECTOR32 },
#define BUSTER_X86_INTRINSIC_Z(name) { #name, X86_INTRINSIC_RETURN_VECTOR64 },
#define BUSTER_X86_INTRINSIC_I(name) { #name, X86_INTRINSIC_RETURN_INTEGER4 },
#define BUSTER_X86_INTRINSIC_B(name) { #name, X86_INTRINSIC_RETURN_INTEGER1 },
#define BUSTER_X86_INTRINSIC_H(name) { #name, X86_INTRINSIC_RETURN_INTEGER2 },
#define BUSTER_X86_INTRINSIC_L(name) { #name, X86_INTRINSIC_RETURN_INTEGER8 },
#define BUSTER_X86_INTRINSIC_V(name) { #name, X86_INTRINSIC_RETURN_NONE },
#define BUSTER_X86_INTRINSIC_INVERTED_B(name) { #name, X86_INTRINSIC_RETURN_INTEGER1, true },

// The controls: intrinsics whose builtins the native programs check already, unmasked, merged and zeroed, which must
// agree with clang's code as the cases do; and the probe's own pair of functions of one intrinsic with its operands
// swapped, whose expressions must differ (run_x86_intrinsic_tests' last check). Then the cases, which no host of the
// tests runs: XOP's own instructions (the fractions, the horizontal sums and differences, the multiply-accumulates,
// VPPERM and the shifts by vectors of counts), AVX-NE-CONVERT's loads, AVX10.2's intrinsics of AVX-VNNI-INT8's and
// AVX-VNNI-INT16's dot products (masked, merged and zeroed, and at 64 bytes), SHA512's, SM3's and SM4's; then AVX10.2's
// saturating conversions, bf16 operations and compares, conversions to and from 8-bit floats and of two vectors of
// floats to halves, and dot products of halves, MOVRS's vector loads and AVX512-BMM's products of bit matrices; then the
// system instructions of extensions no host of the tests has, or whose effects are on the machine's state (a
// transaction's, the user interrupts', the tiles' configuration, MSRs): RAO-INT's, CLDEMOTE, SERIALIZE, UINTR's,
// PTWRITE, UMONITOR, LWP's, MOVRS's integer loads and prefetch, RTM's XEND and XTEST, TSXLDTRK's, AMX-TILE's
// configuration's, ENQCMD's and USER_MSR's, whose effects the traces compare in order with their stores; and the
// leaves' (c_builtin_x86_helpers.c): Key Locker's, whose aesenc and aesdec ones branch on ZF, WAITPKG's UMWAIT and
// TPAUSE, and RTM's XBEGIN, whose paths the traces follow. clang's code of the aesenc and aesdec ones stores the result
// where ZF is 1 and zeros where it is 0, the other way from Intel's manual, clang's own keylockerintrin.h and GCC's
// code, which the compiler follows: their cases compare clang's paths with their conditions negated (inverted). Last,
// AVX10.2's bf16 arithmetic, square roots and fused multiply-adds, and some of their masked forms: in a unit whose target
// hasn't AVX10.2, znver5's, clang computes the arithmetic in float, its excess precision, and folds a negated operand of
// a multiply-add into the form. tools/ci/correctness.tests runs the command again with --march diamondrapids, whose
// target has AVX10.2, and --option -mno-apxf, on tests/x86-intrinsics/avx10.2-units.txt's cases, whose arithmetic is then
// AVX10.2's own.
static const X86IntrinsicCase x86_intrinsic_cases[] = {
    BUSTER_X86_INTRINSIC_X(_mm_add_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_add_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_add_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_dpbusd_avx_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbusd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbusd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_comlt_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_comge_epu32)
    BUSTER_X86_INTRINSIC_V(_mm_mask_store_sbh)
    BUSTER_X86_INTRINSIC_X(buster_intrinsic_sub_ab)
    BUSTER_X86_INTRINSIC_X(buster_intrinsic_sub_ba)
    BUSTER_X86_INTRINSIC_X(_mm_frcz_ps)
    BUSTER_X86_INTRINSIC_X(_mm_frcz_pd)
    BUSTER_X86_INTRINSIC_X(_mm_frcz_ss)
    BUSTER_X86_INTRINSIC_X(_mm_frcz_sd)
    BUSTER_X86_INTRINSIC_Y(_mm256_frcz_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_frcz_pd)
    BUSTER_X86_INTRINSIC_X(_mm_haddw_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_haddd_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_haddd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_haddw_epu8)
    BUSTER_X86_INTRINSIC_X(_mm_haddd_epu8)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epu8)
    BUSTER_X86_INTRINSIC_X(_mm_haddd_epu16)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epu16)
    BUSTER_X86_INTRINSIC_X(_mm_haddq_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_hsubw_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_hsubd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_hsubq_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maccs_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_macc_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_maccsd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_maccd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_maccs_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_macc_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maccslo_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_macclo_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maccshi_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_macchi_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maddsd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_maddd_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_perm_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_shl_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_shl_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_shl_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_shl_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_sha_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_sha_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_sha_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_sha_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_bcstnebf16_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_bcstnebf16_ps)
    BUSTER_X86_INTRINSIC_X(_mm_bcstnesh_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_bcstnesh_ps)
    BUSTER_X86_INTRINSIC_X(_mm_cvtneebf16_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtneebf16_ps)
    BUSTER_X86_INTRINSIC_X(_mm_cvtneeph_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtneeph_ps)
    BUSTER_X86_INTRINSIC_X(_mm_cvtneobf16_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtneobf16_ps)
    BUSTER_X86_INTRINSIC_X(_mm_cvtneoph_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtneoph_ps)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbssd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbssds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbsud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbsuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbuud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpbuuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwsud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwsuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwusd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwusds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwuud_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_dpwuuds_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_sha512msg1_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_sha512msg2_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_sha512rnds2_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_sm3msg1_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_sm3msg2_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_sm4key4_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_sm4rnds4_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_pd_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_pd_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_pd_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_pd_epu64)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_ps_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_ps_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_ps_epu32)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtts_ps_epu64)
    BUSTER_X86_INTRINSIC_Z(_mm512_getexp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_getexp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_rcp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_scalef_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_getexp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_rcp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_scalef_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_max_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_min_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_rcp_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_scalef_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_getexp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_getexp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_rcp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_scalef_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_getexp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_rcp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_scalef_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_max_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_min_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_rcp_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_scalef_pbh)
    BUSTER_X86_INTRINSIC_I(_mm_comieq_sbh)
    BUSTER_X86_INTRINSIC_I(_mm_comige_sbh)
    BUSTER_X86_INTRINSIC_I(_mm_comigt_sbh)
    BUSTER_X86_INTRINSIC_I(_mm_comile_sbh)
    BUSTER_X86_INTRINSIC_I(_mm_comilt_sbh)
    BUSTER_X86_INTRINSIC_I(_mm_comineq_sbh)
    BUSTER_X86_INTRINSIC_X(_mm_getexp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mask_getexp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mask_rcp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mask_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mask_scalef_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_getexp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_rcp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_scalef_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_max_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_min_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_rcp_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_rsqrt_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_scalef_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvt2ph_bf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvt2ph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvthf8_ph)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvtph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvts_2ph_bf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvts_2ph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvthf8_ph)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvtph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_mask_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvthf8_ph)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvtph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm512_maskz_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvt2ph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvt2ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvtph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvts_2ph_bf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvts_2ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvtph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_mask_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_mask_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvtph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm256_maskz_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_X(_mm_cvt2ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvt2ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvtph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_2ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_2ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_mask_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtbiasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtbiasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvthf8_ph)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvts_biasph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvts_biasph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvts_ph_bf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvts_ph_hf8)
    BUSTER_X86_INTRINSIC_X(_mm_maskz_cvtx2ps_ph)
    BUSTER_X86_INTRINSIC_Z(_mm512_dpph_ps)
    BUSTER_X86_INTRINSIC_Y(_mm256_dpph_ps)
    BUSTER_X86_INTRINSIC_X(_mm_dpph_ps)
    BUSTER_X86_INTRINSIC_Z(_mm512_loadrs_epi16)
    BUSTER_X86_INTRINSIC_Z(_mm512_loadrs_epi32)
    BUSTER_X86_INTRINSIC_Z(_mm512_loadrs_epi64)
    BUSTER_X86_INTRINSIC_Z(_mm512_loadrs_epi8)
    BUSTER_X86_INTRINSIC_Y(_mm256_loadrs_epi16)
    BUSTER_X86_INTRINSIC_Y(_mm256_loadrs_epi32)
    BUSTER_X86_INTRINSIC_Y(_mm256_loadrs_epi64)
    BUSTER_X86_INTRINSIC_Y(_mm256_loadrs_epi8)
    BUSTER_X86_INTRINSIC_X(_mm_loadrs_epi16)
    BUSTER_X86_INTRINSIC_X(_mm_loadrs_epi32)
    BUSTER_X86_INTRINSIC_X(_mm_loadrs_epi64)
    BUSTER_X86_INTRINSIC_X(_mm_loadrs_epi8)
    BUSTER_X86_INTRINSIC_Z(_mm512_bmacor16x16x16)
    BUSTER_X86_INTRINSIC_Z(_mm512_bmacxor16x16x16)
    BUSTER_X86_INTRINSIC_Y(_mm256_bmacor16x16x16)
    BUSTER_X86_INTRINSIC_Y(_mm256_bmacxor16x16x16)
    BUSTER_X86_INTRINSIC_V(_aadd_i32)
    BUSTER_X86_INTRINSIC_V(_aadd_i64)
    BUSTER_X86_INTRINSIC_V(_aand_i32)
    BUSTER_X86_INTRINSIC_V(_aand_i64)
    BUSTER_X86_INTRINSIC_V(_aor_i32)
    BUSTER_X86_INTRINSIC_V(_aor_i64)
    BUSTER_X86_INTRINSIC_V(_axor_i32)
    BUSTER_X86_INTRINSIC_V(_axor_i64)
    BUSTER_X86_INTRINSIC_V(_cldemote)
    BUSTER_X86_INTRINSIC_V(_serialize)
    BUSTER_X86_INTRINSIC_V(_clui)
    BUSTER_X86_INTRINSIC_V(_stui)
    BUSTER_X86_INTRINSIC_B(_testui)
    BUSTER_X86_INTRINSIC_V(_senduipi)
    BUSTER_X86_INTRINSIC_V(_ptwrite32)
    BUSTER_X86_INTRINSIC_V(_ptwrite64)
    BUSTER_X86_INTRINSIC_V(_umonitor)
    BUSTER_X86_INTRINSIC_V(__llwpcb)
    BUSTER_X86_INTRINSIC_L(__slwpcb)
    BUSTER_X86_INTRINSIC_B(_movrs_i8)
    BUSTER_X86_INTRINSIC_H(_movrs_i16)
    BUSTER_X86_INTRINSIC_I(_movrs_i32)
    BUSTER_X86_INTRINSIC_L(_movrs_i64)
    BUSTER_X86_INTRINSIC_V(_m_prefetchrs)
    BUSTER_X86_INTRINSIC_V(_xend)
    BUSTER_X86_INTRINSIC_I(_xtest)
    BUSTER_X86_INTRINSIC_V(_xsusldtrk)
    BUSTER_X86_INTRINSIC_V(_xresldtrk)
    BUSTER_X86_INTRINSIC_V(_tile_loadconfig)
    BUSTER_X86_INTRINSIC_V(_tile_storeconfig)
    BUSTER_X86_INTRINSIC_V(_tile_release)
    BUSTER_X86_INTRINSIC_I(_enqcmd)
    BUSTER_X86_INTRINSIC_I(_enqcmds)
    BUSTER_X86_INTRINSIC_L(_urdmsr)
    BUSTER_X86_INTRINSIC_V(_uwrmsr)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesenc128kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesdec128kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesenc256kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesdec256kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesencwide128kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesdecwide128kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesencwide256kl_u8)
    BUSTER_X86_INTRINSIC_INVERTED_B(_mm_aesdecwide256kl_u8)
    BUSTER_X86_INTRINSIC_I(_mm_encodekey128_u32)
    BUSTER_X86_INTRINSIC_I(_mm_encodekey256_u32)
    BUSTER_X86_INTRINSIC_V(_mm_loadiwkey)
    BUSTER_X86_INTRINSIC_B(_umwait)
    BUSTER_X86_INTRINSIC_B(_tpause)
    BUSTER_X86_INTRINSIC_I(_xbegin)
    BUSTER_X86_INTRINSIC_X(_mm_add_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_sub_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mul_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_div_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_sqrt_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_fmadd_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_fmsub_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_fnmadd_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_fnmsub_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_add_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_sub_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_mul_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_div_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_sqrt_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_fmadd_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_fmsub_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_fnmadd_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_fnmsub_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_add_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_sub_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mul_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_div_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_sqrt_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_fmadd_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_fmsub_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_fnmadd_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_fnmsub_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_mask_add_pbh)
    BUSTER_X86_INTRINSIC_Y(_mm256_maskz_div_pbh)
    BUSTER_X86_INTRINSIC_X(_mm_mask3_fmsub_pbh)
    BUSTER_X86_INTRINSIC_Z(_mm512_maskz_fnmadd_pbh)
};

#undef BUSTER_X86_INTRINSIC_X
#undef BUSTER_X86_INTRINSIC_Y
#undef BUSTER_X86_INTRINSIC_Z
#undef BUSTER_X86_INTRINSIC_I
#undef BUSTER_X86_INTRINSIC_B
#undef BUSTER_X86_INTRINSIC_H
#undef BUSTER_X86_INTRINSIC_L
#undef BUSTER_X86_INTRINSIC_V
#undef BUSTER_X86_INTRINSIC_INVERTED_B

// The probe's own functions, after the headers: one intrinsic of two operands, called with them in each order.
static const char x86_intrinsic_probe_functions[] = "__m128i buster_intrinsic_sub_ab(__m128i a, __m128i b) { return _mm_sub_epi32(a, b); }\n"
                                                    "__m128i buster_intrinsic_sub_ba(__m128i a, __m128i b) { return _mm_sub_epi32(b, a); }\n";

// ---------------------------------------------------------------------------------------------------------------------
// The objects

typedef struct X86IntrinsicObject X86IntrinsicObject;
struct X86IntrinsicObject
{
    uint8_t* bytes;
    uint64_t length;
    const Elf64_Shdr* sections;
    uint32_t section_count;
    const Elf64_Sym* symbols;
    uint64_t symbol_count;
    const char* names;
    uint64_t names_size;
};

static bool x86_intrinsic_range(const X86IntrinsicObject* object, uint64_t offset, uint64_t size)
{
    return offset <= object->length && size <= object->length - offset;
}

// The relocatable object at `path`, read whole, with its section headers and symbol table checked to lie in it.
static bool x86_intrinsic_object_read(X86IntrinsicObject* object, const char* path)
{
    *object = (X86IntrinsicObject){ 0 };
    FILE* file = fopen(path, "rb");
    bool valid = file != 0 && fseek(file, 0, SEEK_END) == 0;
    const long size = valid ? ftell(file) : -1;
    valid = valid && size >= (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0;
    object->bytes = valid ? malloc((size_t)size) : 0;
    valid = valid && object->bytes != 0 && fread(object->bytes, 1, (size_t)size, file) == (size_t)size;
    object->length = valid ? (uint64_t)size : 0;
    if (file != 0)
    {
        fclose(file);
    }
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)object->bytes;
    valid = valid && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_type == ET_REL && header->e_shentsize == sizeof(Elf64_Shdr) &&
            header->e_shnum != 0 && x86_intrinsic_range(object, header->e_shoff, (uint64_t)header->e_shnum * sizeof(Elf64_Shdr));
    if (valid)
    {
        object->sections = (const Elf64_Shdr*)(object->bytes + header->e_shoff);
        object->section_count = header->e_shnum;
    }
    for (uint32_t s = 0; valid && s < object->section_count; s += 1)
    {
        const Elf64_Shdr* section = &object->sections[s];
        valid = section->sh_type == SHT_NOBITS || section->sh_type == SHT_NULL || x86_intrinsic_range(object, section->sh_offset, section->sh_size);
        if (valid && section->sh_type == SHT_SYMTAB)
        {
            const Elf64_Shdr* strings = section->sh_link < object->section_count ? &object->sections[section->sh_link] : 0;
            valid = strings != 0 && section->sh_entsize == sizeof(Elf64_Sym) && x86_intrinsic_range(object, strings->sh_offset, strings->sh_size);
            object->symbols = valid ? (const Elf64_Sym*)(object->bytes + section->sh_offset) : 0;
            object->symbol_count = valid ? section->sh_size / sizeof(Elf64_Sym) : 0;
            object->names = valid ? (const char*)object->bytes + strings->sh_offset : 0;
            object->names_size = valid ? strings->sh_size : 0;
        }
    }
    valid = valid && object->symbols != 0;
    if (!valid)
    {
        free(object->bytes);
        *object = (X86IntrinsicObject){ 0 };
    }
    return valid;
}

static const char* x86_intrinsic_symbol_name(const X86IntrinsicObject* object, const Elf64_Sym* symbol)
{
    const uint64_t offset = symbol->st_name;
    return offset < object->names_size && memchr(object->names + offset, 0, object->names_size - offset) != 0 ? object->names + offset : "";
}

// The function named `name` that the object defines in a section of its own bytes, or 0.
static const Elf64_Sym* x86_intrinsic_function_named(const X86IntrinsicObject* object, const char* name)
{
    const Elf64_Sym* result = 0;
    for (uint64_t i = 1; result == 0 && i < object->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &object->symbols[i];
        const bool defined = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx != SHN_UNDEF && symbol->st_shndx < object->section_count &&
                             object->sections[symbol->st_shndx].sh_type == SHT_PROGBITS;
        result = defined && strcmp(x86_intrinsic_symbol_name(object, symbol), name) == 0 ? symbol : 0;
    }
    return result;
}

// The function of the object at `offset` in section `section`, or 0.
static const Elf64_Sym* x86_intrinsic_function_at(const X86IntrinsicObject* object, uint32_t section, uint64_t offset)
{
    const Elf64_Sym* result = 0;
    for (uint64_t i = 1; result == 0 && i < object->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &object->symbols[i];
        result = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx == section && symbol->st_value == offset ? symbol : 0;
    }
    return result;
}

// The relocation of the field at `offset` of section `section`, if any.
static const Elf64_Rela* x86_intrinsic_relocation(const X86IntrinsicObject* object, uint32_t section, uint64_t offset)
{
    const Elf64_Rela* result = 0;
    for (uint32_t s = 0; result == 0 && s < object->section_count; s += 1)
    {
        const Elf64_Shdr* table = &object->sections[s];
        if (table->sh_type == SHT_RELA && table->sh_info == section && table->sh_entsize == sizeof(Elf64_Rela))
        {
            const Elf64_Rela* relocations = (const Elf64_Rela*)(object->bytes + table->sh_offset);
            const uint64_t count = table->sh_size / sizeof(Elf64_Rela);
            for (uint64_t r = 0; result == 0 && r < count; r += 1)
            {
                result = relocations[r].r_offset == offset ? &relocations[r] : 0;
            }
        }
    }
    return result;
}

// Where a PC-relative relocation's symbol and addend point, as a section and an offset in it, for a field that ends
// `distance` bytes before the instruction does (its immediate's bytes): S + A + 4 + distance.
static bool x86_intrinsic_relocation_target(const X86IntrinsicObject* object, const Elf64_Rela* relocation, uint64_t distance, uint32_t* section,
                                            uint64_t* offset)
{
    const uint32_t type = ELF64_R_TYPE(relocation->r_info);
    const uint64_t index = ELF64_R_SYM(relocation->r_info);
    const bool valid = (type == R_X86_64_PC32 || type == R_X86_64_PLT32) && index < object->symbol_count &&
                       object->symbols[index].st_shndx != SHN_UNDEF && object->symbols[index].st_shndx < object->section_count;
    if (valid)
    {
        *section = object->symbols[index].st_shndx;
        *offset = object->symbols[index].st_value + (uint64_t)relocation->r_addend + 4 + distance;
    }
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The trace's values

// A node: an argument's register or stack slot at the entry (its text g0 to g5 for RDI, RSI, RDX, RCX, R8 and R9, v0 to
// v7 for ZMM0 to ZMM7, s0 and on for the stack's 8-byte slots past the return address), a constant (its bytes), a
// stack address (the entry's RSP plus an offset), or a value computed from others (an instruction's result, a select,
// a load, a slice or a join of others' bytes), whose text says which. Node 0 is a byte nothing defined, node 1 zero.
typedef enum X86IntrinsicKind
{
    X86_INTRINSIC_UNKNOWN,
    X86_INTRINSIC_ZERO,
    X86_INTRINSIC_ARGUMENT,
    X86_INTRINSIC_CONSTANT,
    X86_INTRINSIC_FRAME,
    X86_INTRINSIC_VALUE,
} X86IntrinsicKind;

#define BUSTER_X86_INTRINSIC_UNKNOWN 0u
#define BUSTER_X86_INTRINSIC_ZERO 1u
#define BUSTER_X86_INTRINSIC_NODES 8192u
#define BUSTER_X86_INTRINSIC_TEXT (4u << 20)
#define BUSTER_X86_INTRINSIC_PIECE 16384u
// The stack, as the trace places it: the entry's RSP, 8 below a 16-byte boundary (the return address's push), the
// bytes below it the functions' frames may reach, and those above it, the return address's and the stack arguments'.
#define BUSTER_X86_INTRINSIC_STACK_TOP 0x1000000ull
#define BUSTER_X86_INTRINSIC_ENTRY (BUSTER_X86_INTRINSIC_STACK_TOP - 8)
#define BUSTER_X86_INTRINSIC_BELOW 0x40000u
#define BUSTER_X86_INTRINSIC_ABOVE 0x400u
#define BUSTER_X86_INTRINSIC_STACK_BASE (BUSTER_X86_INTRINSIC_STACK_TOP - BUSTER_X86_INTRINSIC_BELOW)
#define BUSTER_X86_INTRINSIC_STACK_SIZE (BUSTER_X86_INTRINSIC_BELOW + BUSTER_X86_INTRINSIC_ABOVE)
#define BUSTER_X86_INTRINSIC_CALLS 8u
#define BUSTER_X86_INTRINSIC_STEPS 8192u
// The paths a trace follows (x86_intrinsic_trace), the conditions of one, and the functions it decodes.
#define BUSTER_X86_INTRINSIC_PATHS 8u
#define BUSTER_X86_INTRINSIC_CONDITIONS 4u
#define BUSTER_X86_INTRINSIC_FUNCTIONS 16u

typedef struct X86IntrinsicByte X86IntrinsicByte;
struct X86IntrinsicByte
{
    uint32_t node;
    uint32_t index;
};

typedef struct X86IntrinsicNode X86IntrinsicNode;
struct X86IntrinsicNode
{
    uint32_t text;
    uint32_t length;
    uint32_t kind;
    uint32_t bytes;
    uint64_t address;
    uint8_t constant[64];
    // An integer operation's results of its first 1, 2 and 4 bytes, of its operands' first bytes (x86_intrinsic_alu),
    // or 0.
    uint32_t narrow[3];
    // A byte that is 0 or 1: a condition's (set<cc>), or an and or an or of them.
    bool boolean;
    // A condition's: its condition code plus one (0 for any other node) and the flags it tests (x86_intrinsic_set_of).
    uint8_t condition;
    uint32_t tested;
};

typedef struct X86IntrinsicTrace X86IntrinsicTrace;
struct X86IntrinsicTrace
{
    const X86IntrinsicObject* object;
    X86IntrinsicNode* nodes;
    uint32_t node_count;
    char* text;
    uint32_t text_length;
    X86IntrinsicByte gpr[16][8];
    X86IntrinsicByte vector[32][64];
    X86IntrinsicByte opmask[8][8];
    uint32_t flags;
    // The boolean that a test with 1 or with itself, or a compare with 0, gave the flags of (ZF its negation), or 0.
    uint32_t flags_boolean;
    X86IntrinsicByte* stack;
    // The stores through pointers, one text after another, and the next store's number.
    char* stores;
    uint32_t stores_length;
    // The path's conditions so far (x86_intrinsic_branch's): each a condition's node and whether it holds.
    uint32_t conditions[BUSTER_X86_INTRINSIC_CONDITIONS];
    bool holds[BUSTER_X86_INTRINSIC_CONDITIONS];
    uint32_t condition_count;
    // A branch's (x86_intrinsic_step's X86_INTRINSIC_BRANCH): its condition's node, whether it holds where the branch
    // is taken, and whether it is xbegin's, whose taken path (its fallback) has the abort status in EAX.
    uint32_t branch_condition;
    bool branch_holds;
    bool branch_abort;
    // Why the trace stopped, where it did: 0 while it goes on.
    char failure[512];
    bool failed;
};

// The forms without effects that the traces met, which the command lists at its end.
static uint16_t x86_intrinsic_uncovered[256];
static uint32_t x86_intrinsic_uncovered_count;

static void x86_intrinsic_fail(X86IntrinsicTrace* trace, const char* format, const char* detail)
{
    if (!trace->failed)
    {
        snprintf(trace->failure, sizeof(trace->failure), format, detail);
        trace->failed = true;
    }
}

// A piece of text being built, as long as BUSTER_X86_INTRINSIC_PIECE at most.
typedef struct X86IntrinsicPiece X86IntrinsicPiece;
struct X86IntrinsicPiece
{
    char data[BUSTER_X86_INTRINSIC_PIECE];
    uint32_t length;
    bool full;
};

static void x86_intrinsic_append(X86IntrinsicPiece* piece, const char* text, uint32_t length)
{
    if (!piece->full && length < sizeof(piece->data) - piece->length)
    {
        memcpy(piece->data + piece->length, text, length);
        piece->length += length;
    }
    else
    {
        piece->full = true;
    }
}

static void x86_intrinsic_appendf(X86IntrinsicPiece* piece, const char* format, uint64_t value)
{
    char text[64];
    const int length = snprintf(text, sizeof(text), format, (unsigned long long)value);
    x86_intrinsic_append(piece, text, length > 0 ? (uint32_t)length : 0);
}

static void x86_intrinsic_append_node(X86IntrinsicPiece* piece, const X86IntrinsicTrace* trace, uint32_t node)
{
    x86_intrinsic_append(piece, trace->text + trace->nodes[node].text, trace->nodes[node].length);
}

// Node text is a slice, not a terminated string. Equal prefixes use their lengths, without reading either tail.
static int x86_intrinsic_node_compare(const X86IntrinsicTrace* trace, uint32_t left, uint32_t right)
{
    const X86IntrinsicNode* a = &trace->nodes[left];
    const X86IntrinsicNode* b = &trace->nodes[right];
    const uint32_t shorter = a->length < b->length ? a->length : b->length;
    const int order = memcmp(trace->text + a->text, trace->text + b->text, shorter);
    return order != 0 ? order : (a->length > b->length) - (a->length < b->length);
}

static uint32_t x86_intrinsic_node(X86IntrinsicTrace* trace, uint32_t kind, uint32_t bytes, const X86IntrinsicPiece* piece)
{
    uint32_t result = BUSTER_X86_INTRINSIC_UNKNOWN;
    if (piece->full || trace->node_count >= BUSTER_X86_INTRINSIC_NODES || piece->length > BUSTER_X86_INTRINSIC_TEXT - trace->text_length)
    {
        x86_intrinsic_fail(trace, "the trace's values are longer than it keeps%s", "");
    }
    else
    {
        result = trace->node_count;
        trace->node_count += 1;
        trace->nodes[result] = (X86IntrinsicNode){ .text = trace->text_length, .length = piece->length, .kind = kind, .bytes = bytes };
        memcpy(trace->text + trace->text_length, piece->data, piece->length);
        trace->text_length += piece->length;
    }
    return result;
}

static uint32_t x86_intrinsic_named(X86IntrinsicTrace* trace, uint32_t kind, uint32_t bytes, const char* text)
{
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    x86_intrinsic_append(&piece, text, (uint32_t)strlen(text));
    return x86_intrinsic_node(trace, kind, bytes, &piece);
}

static uint32_t x86_intrinsic_frame(X86IntrinsicTrace* trace, uint64_t address)
{
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    const int64_t offset = (int64_t)(address - BUSTER_X86_INTRINSIC_ENTRY);
    x86_intrinsic_appendf(&piece, offset < 0 ? "frame-%llu" : "frame+%llu", (uint64_t)(offset < 0 ? -offset : offset));
    const uint32_t node = x86_intrinsic_node(trace, X86_INTRINSIC_FRAME, 8, &piece);
    trace->nodes[node].address = address;
    return node;
}

static void x86_intrinsic_fill(X86IntrinsicByte* bytes, uint32_t count, uint32_t node)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        bytes[i] = (X86IntrinsicByte){ .node = node, .index = i };
    }
}

static void x86_intrinsic_zero(X86IntrinsicByte* bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        bytes[i] = (X86IntrinsicByte){ .node = BUSTER_X86_INTRINSIC_ZERO };
    }
}

static bool x86_intrinsic_constant_byte(const X86IntrinsicTrace* trace, X86IntrinsicByte byte, uint8_t* value)
{
    const bool constant = byte.node == BUSTER_X86_INTRINSIC_ZERO || trace->nodes[byte.node].kind == X86_INTRINSIC_CONSTANT;
    *value = byte.node == BUSTER_X86_INTRINSIC_ZERO || !constant ? 0 : trace->nodes[byte.node].constant[byte.index];
    return constant;
}

// A constant of `count` bytes (to 64): its text 0 where they are all zero, and otherwise # and its bytes in
// hexadecimal, the last one first.
static uint32_t x86_intrinsic_constant(X86IntrinsicTrace* trace, const uint8_t* bytes, uint32_t count)
{
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    bool zero = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        zero = zero && bytes[i] == 0;
    }
    if (zero)
    {
        x86_intrinsic_append(&piece, "0", 1);
    }
    else
    {
        x86_intrinsic_append(&piece, "#", 1);
        for (uint32_t i = count; i > 0; i -= 1)
        {
            x86_intrinsic_appendf(&piece, "%02llx", bytes[i - 1]);
        }
    }
    const uint32_t node = x86_intrinsic_node(trace, X86_INTRINSIC_CONSTANT, count, &piece);
    memcpy(trace->nodes[node].constant, bytes, count);
    return node;
}

// The text of a run of bytes, `count` bytes of node `node` from byte `first`: the node's own where they are all of it,
// node:n for its first n, and node[first:n] otherwise.
static void x86_intrinsic_append_run(X86IntrinsicPiece* piece, const X86IntrinsicTrace* trace, uint32_t node, uint32_t first, uint32_t count)
{
    if (node == BUSTER_X86_INTRINSIC_UNKNOWN || node == BUSTER_X86_INTRINSIC_ZERO)
    {
        x86_intrinsic_append(piece, node == BUSTER_X86_INTRINSIC_ZERO ? "0*" : "?*", 2);
        x86_intrinsic_appendf(piece, "%llu", count);
    }
    else
    {
        x86_intrinsic_append_node(piece, trace, node);
        if (first == 0 && count < trace->nodes[node].bytes)
        {
            x86_intrinsic_appendf(piece, ":%llu", count);
        }
        else if (first != 0 || count != trace->nodes[node].bytes)
        {
            x86_intrinsic_appendf(piece, "[%llu:", first);
            x86_intrinsic_appendf(piece, "%llu]", count);
        }
    }
}

// The value of `count` bytes (to 64): a constant where they all are, the node itself where they are all of one, in
// order, and otherwise a join of their runs, cat(run, ...), each run's text x86_intrinsic_append_run's.
static uint32_t x86_intrinsic_value(X86IntrinsicTrace* trace, const X86IntrinsicByte* bytes, uint32_t count)
{
    uint32_t result = BUSTER_X86_INTRINSIC_UNKNOWN;
    uint8_t constant[64] = { 0 };
    bool constants = count <= 64;
    for (uint32_t i = 0; constants && i < count; i += 1)
    {
        constants = x86_intrinsic_constant_byte(trace, bytes[i], &constant[i]);
    }
    bool whole = count != 0 && bytes[0].node > BUSTER_X86_INTRINSIC_ZERO && bytes[0].index == 0 && trace->nodes[bytes[0].node].bytes == count;
    for (uint32_t i = 1; whole && i < count; i += 1)
    {
        whole = bytes[i].node == bytes[0].node && bytes[i].index == i;
    }
    bool prefix = count != 0 && count < 8 && bytes[0].node > BUSTER_X86_INTRINSIC_ZERO && bytes[0].index == 0 && count < trace->nodes[bytes[0].node].bytes;
    for (uint32_t i = 1; prefix && i < count; i += 1)
    {
        prefix = bytes[i].node == bytes[0].node && bytes[i].index == i;
    }
    const uint32_t narrow = prefix && (count == 1 || count == 2 || count == 4) ? trace->nodes[bytes[0].node].narrow[count == 1 ? 0 : count == 2 ? 1 : 2] : 0;
    if (constants)
    {
        result = x86_intrinsic_constant(trace, constant, count);
    }
    else if (whole)
    {
        result = bytes[0].node;
    }
    else if (narrow != 0)
    {
        result = narrow;
    }
    else
    {
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        uint32_t runs = 0;
        for (uint32_t i = 0; i < count;)
        {
            uint32_t end = i + 1;
            const bool plain = bytes[i].node <= BUSTER_X86_INTRINSIC_ZERO;
            while (end < count && bytes[end].node == bytes[i].node && (plain || bytes[end].index == bytes[i].index + (end - i)))
            {
                end += 1;
            }
            runs += 1;
            i = end;
        }
        if (runs > 1)
        {
            x86_intrinsic_append(&piece, "cat(", 4);
        }
        for (uint32_t i = 0, run = 0; i < count; run += 1)
        {
            uint32_t end = i + 1;
            const bool plain = bytes[i].node <= BUSTER_X86_INTRINSIC_ZERO;
            while (end < count && bytes[end].node == bytes[i].node && (plain || bytes[end].index == bytes[i].index + (end - i)))
            {
                end += 1;
            }
            if (run != 0)
            {
                x86_intrinsic_append(&piece, ",", 1);
            }
            x86_intrinsic_append_run(&piece, trace, bytes[i].node, bytes[i].index, end - i);
            i = end;
        }
        if (runs > 1)
        {
            x86_intrinsic_append(&piece, ")", 1);
        }
        result = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, count, &piece);
    }
    return result;
}

// A stack address's node, if `bytes` are one's whole.
static uint32_t x86_intrinsic_frame_of(const X86IntrinsicTrace* trace, const X86IntrinsicByte* bytes)
{
    bool frame = bytes[0].node > BUSTER_X86_INTRINSIC_ZERO && trace->nodes[bytes[0].node].kind == X86_INTRINSIC_FRAME;
    for (uint32_t i = 0; frame && i < 8; i += 1)
    {
        frame = bytes[i].node == bytes[0].node && bytes[i].index == i;
    }
    return frame ? bytes[0].node : BUSTER_X86_INTRINSIC_UNKNOWN;
}

static X86IntrinsicByte* x86_intrinsic_stack(X86IntrinsicTrace* trace, uint64_t address, uint32_t size)
{
    X86IntrinsicByte* result = 0;
    if (address >= BUSTER_X86_INTRINSIC_STACK_BASE && address - BUSTER_X86_INTRINSIC_STACK_BASE <= BUSTER_X86_INTRINSIC_STACK_SIZE - size)
    {
        result = &trace->stack[address - BUSTER_X86_INTRINSIC_STACK_BASE];
    }
    else
    {
        x86_intrinsic_fail(trace, "a stack access outside the trace's stack%s", "");
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Operands

// Each form's canonical number: the first form of its mnemonic, operands, immediate, syntax, vector length, broadcast,
// rounding and relative bytes, so that a VEX and an EVEX encoding of one instruction (vpaddd of XMM registers, whose
// EVEX form clang's code doesn't use where no opmask, broadcast or register past XMM15 needs it) are one node. A ModRM.rm
// operand of a register or memory compares as its register's (x86_intrinsic_rm_kind): VEX's shifts by an immediate take
// a register alone, and EVEX's either.
static uint16_t x86_intrinsic_canonical[X86_FORM_COUNT];

static uint64_t x86_intrinsic_rm_kind(uint16_t rm)
{
    return (rm & BUSTER_X86_OPERAND_REGISTER) != 0 ? (uint64_t)(rm & ~BUSTER_X86_OPERAND_MEMORY) : rm;
}

// The order of two forms by what makes them one instruction (their mnemonic first), 0 where they are one.
static int x86_intrinsic_instruction_order(uint32_t a, uint32_t b)
{
    const X86FormDescription* x = &x86_form_descriptions[a];
    const X86FormDescription* y = &x86_form_descriptions[b];
    const uint32_t flags = BUSTER_X86_EVEX_BROADCAST | BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE;
    const uint64_t first = (uint64_t)x->reg << 48 | x86_intrinsic_rm_kind(x->rm) << 32 | (uint64_t)x->vvvv << 16 | (uint64_t)x->operand_bytes << 8 |
                           x->immediate_bytes;
    const uint64_t second = (uint64_t)y->reg << 48 | x86_intrinsic_rm_kind(y->rm) << 32 | (uint64_t)y->vvvv << 16 | (uint64_t)y->operand_bytes << 8 |
                            y->immediate_bytes;
    const uint64_t third = (uint64_t)x->syntax << 32 | (uint64_t)x->vector_length << 24 | (uint64_t)(x->evex & flags) << 8 | x->relative_bytes;
    const uint64_t fourth = (uint64_t)y->syntax << 32 | (uint64_t)y->vector_length << 24 | (uint64_t)(y->evex & flags) << 8 | y->relative_bytes;
    const int named = strcmp(x->mnemonic, y->mnemonic);
    return named != 0 ? named : first != second ? (first < second ? -1 : 1) : third != fourth ? (third < fourth ? -1 : 1) : 0;
}

static int x86_intrinsic_form_order(const void* left, const void* right)
{
    const uint16_t a = *(const uint16_t*)left;
    const uint16_t b = *(const uint16_t*)right;
    const int order = x86_intrinsic_instruction_order(a, b);
    return order != 0 ? order : a < b ? -1 : a > b ? 1 : 0;
}

static void x86_intrinsic_canonical_forms(void)
{
    static uint16_t order[X86_FORM_COUNT];
    for (uint32_t row = 0; row < X86_FORM_COUNT; row += 1)
    {
        order[row] = (uint16_t)row;
    }
    qsort(order + 1, X86_FORM_COUNT - 1, sizeof(order[0]), x86_intrinsic_form_order);
    uint16_t first = 0;
    for (uint32_t i = 1; i < X86_FORM_COUNT; i += 1)
    {
        first = i == 1 || x86_intrinsic_instruction_order(first, order[i]) != 0 ? order[i] : first;
        x86_intrinsic_canonical[order[i]] = first;
    }
}

// The bytes a form's memory operand reads or writes: a broadcast form's element, or the operand's.
static uint32_t x86_intrinsic_memory_bytes(const X86FormDescription* form)
{
    return form->family == BUSTER_X86_FAMILY_EVEX && (form->evex & BUSTER_X86_EVEX_BROADCAST) != 0 ? form->disp8_scale : form->operand_bytes;
}

// The same, or where the form's table gives none, its effects' width: the 64 bytes of ldtilecfg, sttilecfg and enqcmd.
static uint32_t x86_intrinsic_memory_size(const X86FormDescription* form, const X86Effects* effects)
{
    const uint32_t bytes = x86_intrinsic_memory_bytes(form);
    return bytes != 0 ? bytes : effects->width;
}

// An explicit operand of a record: by role (ModRM.reg, ModRM.rm, VEX.vvvv, the /is4 register), what it is, its
// register or none, its bytes, and whether the form reads it and writes it.
typedef enum X86IntrinsicPlace
{
    X86_INTRINSIC_PLACE_NONE,
    X86_INTRINSIC_PLACE_GPR,
    X86_INTRINSIC_PLACE_VECTOR,
    X86_INTRINSIC_PLACE_OPMASK,
    X86_INTRINSIC_PLACE_MEMORY,
    X86_INTRINSIC_PLACE_OTHER,
} X86IntrinsicPlace;

#define BUSTER_X86_INTRINSIC_ROLE_IS4 3u
#define BUSTER_X86_INTRINSIC_ROLES 4u

typedef struct X86IntrinsicOperand X86IntrinsicOperand;
struct X86IntrinsicOperand
{
    uint8_t place;
    uint8_t number;
    uint8_t bytes;
    uint8_t offset;
    bool read;
    bool write;
};

static X86IntrinsicOperand x86_intrinsic_register(uint16_t kind, uint8_t number)
{
    X86IntrinsicOperand operand = { 0 };
    switch (kind & BUSTER_X86_OPERAND_CLASS)
    {
    case BUSTER_X86_CLASS_GPR8:
    case BUSTER_X86_CLASS_GPR16:
    case BUSTER_X86_CLASS_GPR32:
    case BUSTER_X86_CLASS_GPR64:
        operand.place = X86_INTRINSIC_PLACE_GPR;
        operand.bytes = (uint8_t)(1u << ((kind & BUSTER_X86_OPERAND_CLASS) - BUSTER_X86_CLASS_GPR8));
        operand.number = (number & BUSTER_X86_REGISTER_HIGH_BYTE) ? (uint8_t)((number & BUSTER_X86_REGISTER_NUMBER) - 4) : (uint8_t)(number & 15);
        operand.offset = (number & BUSTER_X86_REGISTER_HIGH_BYTE) ? 1 : 0;
        break;
    case BUSTER_X86_CLASS_XMM:
    case BUSTER_X86_CLASS_YMM:
    case BUSTER_X86_CLASS_ZMM:
        operand.place = X86_INTRINSIC_PLACE_VECTOR;
        operand.bytes = (uint8_t)(16u << ((kind & BUSTER_X86_OPERAND_CLASS) - BUSTER_X86_CLASS_XMM));
        operand.number = number & 31;
        break;
    case BUSTER_X86_CLASS_OPMASK:
        operand.place = X86_INTRINSIC_PLACE_OPMASK;
        operand.bytes = 8;
        operand.number = number & 7;
        break;
    default: operand.place = X86_INTRINSIC_PLACE_OTHER; break;
    }
    return operand;
}

// The record's operands by role, and whether its form has an /is4 register.
static void x86_intrinsic_operands(const X86Instruction* instruction, const X86FormDescription* form, const X86Effects* effects,
                                   X86IntrinsicOperand* operands)
{
    const uint16_t kinds[3] = { form->reg, form->rm, form->vvvv };
    const uint8_t numbers[3] = { instruction->reg, instruction->rm, instruction->vvvv };
    for (uint32_t role = 0; role < 3; role += 1)
    {
        X86IntrinsicOperand operand = { 0 };
        if (role == BUSTER_X86_ROLE_RM && (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0 && (kinds[role] & BUSTER_X86_OPERAND_MEMORY) != 0)
        {
            operand.place = X86_INTRINSIC_PLACE_MEMORY;
            operand.bytes = form->operand_bytes;
        }
        else if ((kinds[role] & BUSTER_X86_OPERAND_REGISTER) != 0)
        {
            operand = x86_intrinsic_register(kinds[role], numbers[role]);
        }
        const uint32_t bits = x86_effects_role(effects, role);
        operand.read = operand.place != X86_INTRINSIC_PLACE_NONE && (bits & BUSTER_X86_ROLE_READ) != 0;
        operand.write = operand.place != X86_INTRINSIC_PLACE_NONE && (bits & BUSTER_X86_ROLE_WRITE) != 0;
        operands[role] = operand;
    }
    const bool is4 = form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4 || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM || form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4_IMM ||
                     form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM;
    operands[BUSTER_X86_INTRINSIC_ROLE_IS4] = (X86IntrinsicOperand){ 0 };
    if (is4)
    {
        X86IntrinsicOperand operand = x86_intrinsic_register(form->reg, (uint8_t)((uint32_t)instruction->immediate >> 4 & 15));
        operand.read = true;
        operands[BUSTER_X86_INTRINSIC_ROLE_IS4] = operand;
    }
}

// The bytes of a register operand, as the trace has them.
static X86IntrinsicByte* x86_intrinsic_register_bytes(X86IntrinsicTrace* trace, const X86IntrinsicOperand* operand)
{
    X86IntrinsicByte* result = 0;
    switch (operand->place)
    {
    case X86_INTRINSIC_PLACE_GPR: result = &trace->gpr[operand->number][operand->offset]; break;
    case X86_INTRINSIC_PLACE_VECTOR: result = trace->vector[operand->number]; break;
    case X86_INTRINSIC_PLACE_OPMASK: result = trace->opmask[operand->number]; break;
    default: break;
    }
    return result;
}

// Writes `count` bytes into a register operand, and the rest of the register as a write of the operand's bytes does
// to it: a 32-bit general register zeroes its upper half and an 8- or 16-bit one keeps it; a vector register's bytes
// past `count` up to the operand's, as `rest` (BUSTER_X86_REST_*) says, and past the operand's, zeroed by a VEX or
// EVEX form and kept by a legacy one (keep_upper); an opmask register's past `count`, zeroed.
static void x86_intrinsic_write(X86IntrinsicTrace* trace, const X86IntrinsicOperand* operand, const X86IntrinsicByte* bytes, uint32_t count, uint32_t rest,
                                bool keep_upper)
{
    X86IntrinsicByte* destination = x86_intrinsic_register_bytes(trace, operand);
    const uint32_t size = operand->bytes;
    if (destination != 0)
    {
        memmove(destination, bytes, (count < size ? count : size) * sizeof(*bytes));
    }
    if (operand->place == X86_INTRINSIC_PLACE_GPR && size == 4 && count >= 4)
    {
        x86_intrinsic_zero(&trace->gpr[operand->number][4], 4);
    }
    else if (operand->place == X86_INTRINSIC_PLACE_GPR && size == 8 && count < 8)
    {
        x86_intrinsic_zero(&trace->gpr[operand->number][count], 8 - count);
    }
    else if (operand->place == X86_INTRINSIC_PLACE_GPR && size == 4 && count < 4)
    {
        x86_intrinsic_zero(&trace->gpr[operand->number][count], 8 - count);
    }
    else if (operand->place == X86_INTRINSIC_PLACE_VECTOR)
    {
        if (count < size && (rest == BUSTER_X86_REST_ZERO || rest == BUSTER_X86_REST_WHOLE))
        {
            x86_intrinsic_zero(&destination[count], size - count);
        }
        if (!keep_upper && size < 64)
        {
            x86_intrinsic_zero(&destination[size], 64 - size);
        }
    }
    else if (operand->place == X86_INTRINSIC_PLACE_OPMASK && count < 8)
    {
        x86_intrinsic_zero(&destination[count], 8 - count);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory

// Where a memory operand is: on the stack (its address), or elsewhere (the node of the address's text), or constant
// data (its bytes, from the object's section that a relocation names).
typedef struct X86IntrinsicMemory X86IntrinsicMemory;
struct X86IntrinsicMemory
{
    bool stack;
    bool constant;
    uint64_t address;
    uint32_t node;
    const uint8_t* data;
};

typedef struct X86IntrinsicPlaceInCode X86IntrinsicPlaceInCode;
struct X86IntrinsicPlaceInCode
{
    uint32_t section;
    uint64_t offset;
    uint32_t length;
};

static X86IntrinsicMemory x86_intrinsic_address(X86IntrinsicTrace* trace, const X86Decoded* decoded, const X86FormDescription* form, X86IntrinsicPlaceInCode place,
                                                uint32_t size)
{
    X86IntrinsicMemory memory = { 0 };
    const X86Instruction* instruction = &decoded->instruction;
    const uint8_t mode = instruction->mode;
    if ((mode & BUSTER_X86_MODE_RIP) != 0)
    {
        // The displacement's field ends the immediate's bytes before the instruction.
        const uint64_t field = place.offset + place.length - 4 - form->immediate_bytes;
        const Elf64_Rela* relocation = x86_intrinsic_relocation(trace->object, place.section, field);
        uint32_t section = place.section;
        uint64_t offset = place.offset + place.length + (uint64_t)(int64_t)instruction->displacement;
        const bool found = relocation != 0 ? x86_intrinsic_relocation_target(trace->object, relocation, form->immediate_bytes, &section, &offset) : true;
        const Elf64_Shdr* target = found ? &trace->object->sections[section] : 0;
        if (target != 0 && target->sh_type == SHT_PROGBITS && offset <= target->sh_size && size <= target->sh_size - offset)
        {
            memory.constant = true;
            memory.data = trace->object->bytes + target->sh_offset + offset;
        }
        else
        {
            x86_intrinsic_fail(trace, "a RIP-relative operand whose data the trace can't read%s", "");
        }
    }
    else if ((mode & BUSTER_X86_MODE_BASE) != 0)
    {
        const uint32_t base = instruction->rm & 15;
        const uint32_t frame = x86_intrinsic_frame_of(trace, trace->gpr[base]);
        const bool indexed = (mode & BUSTER_X86_MODE_INDEX) != 0;
        uint8_t index[8] = { 0 };
        bool constant = true;
        for (uint32_t i = 0; indexed && constant && i < 8; i += 1)
        {
            constant = x86_intrinsic_constant_byte(trace, trace->gpr[instruction->index & 15][i], &index[i]);
        }
        uint64_t offset = 0;
        memcpy(&offset, index, sizeof(offset));
        if (frame != BUSTER_X86_INTRINSIC_UNKNOWN && (!indexed || constant))
        {
            memory.stack = true;
            memory.address = trace->nodes[frame].address + (uint64_t)(int64_t)instruction->displacement + (indexed ? offset << (mode & BUSTER_X86_MODE_SCALE) : 0);
        }
        else
        {
            static X86IntrinsicPiece piece;
            piece.length = 0;
            piece.full = false;
            x86_intrinsic_append(&piece, "[", 1);
            x86_intrinsic_append_node(&piece, trace, x86_intrinsic_value(trace, trace->gpr[base], 8));
            if (indexed)
            {
                x86_intrinsic_append(&piece, "+", 1);
                x86_intrinsic_append_node(&piece, trace, x86_intrinsic_value(trace, trace->gpr[instruction->index & 15], 8));
                x86_intrinsic_appendf(&piece, "*%llu", 1ull << (mode & BUSTER_X86_MODE_SCALE));
            }
            if (instruction->displacement != 0)
            {
                x86_intrinsic_appendf(&piece, "%+lld", (uint64_t)(int64_t)instruction->displacement);
            }
            x86_intrinsic_append(&piece, "]", 1);
            memory.node = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 8, &piece);
        }
    }
    else
    {
        x86_intrinsic_fail(trace, "a memory operand without a base%s", "");
    }
    return memory;
}

// The value of `size` bytes at a memory operand: the stack's bytes, the constant's, or a load of the address.
static uint32_t x86_intrinsic_load(X86IntrinsicTrace* trace, const X86IntrinsicMemory* memory, uint32_t size, X86IntrinsicByte* bytes)
{
    uint32_t node = BUSTER_X86_INTRINSIC_UNKNOWN;
    if (memory->stack)
    {
        const X86IntrinsicByte* stack = x86_intrinsic_stack(trace, memory->address, size);
        if (stack != 0)
        {
            memcpy(bytes, stack, size * sizeof(*bytes));
            node = x86_intrinsic_value(trace, bytes, size);
        }
    }
    else if (memory->constant)
    {
        node = x86_intrinsic_constant(trace, memory->data, size);
        x86_intrinsic_fill(bytes, size, node);
    }
    else if (!trace->failed)
    {
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_appendf(&piece, "load%llu", size);
        x86_intrinsic_append_node(&piece, trace, memory->node);
        node = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, size, &piece);
        x86_intrinsic_fill(bytes, size, node);
    }
    return node;
}

// Adds a line to the trace's stores, which keep its stores and its effects in their order.
static void x86_intrinsic_keep(X86IntrinsicTrace* trace, const X86IntrinsicPiece* piece)
{
    if (piece->full || piece->length > BUSTER_X86_INTRINSIC_TEXT - trace->stores_length)
    {
        x86_intrinsic_fail(trace, "the trace's stores are longer than it keeps%s", "");
    }
    else
    {
        memcpy(trace->stores + trace->stores_length, piece->data, piece->length);
        trace->stores_length += piece->length;
    }
}

// An instruction's node as an effect among the stores: one that writes nothing the trace follows (a fence, a system
// instruction, a prefetch's or a cache line's hint), whose operands' values and place in order its code must have too.
static void x86_intrinsic_effect(X86IntrinsicTrace* trace, uint32_t node)
{
    if (!trace->failed)
    {
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_append(&piece, "effect ", 7);
        x86_intrinsic_append_node(&piece, trace, node);
        x86_intrinsic_append(&piece, "\n", 1);
        x86_intrinsic_keep(trace, &piece);
    }
}

// Stores `size` bytes at a memory operand: on the stack, its bytes; elsewhere, a store of the value, its text added to
// the trace's stores.
static void x86_intrinsic_store(X86IntrinsicTrace* trace, const X86IntrinsicMemory* memory, const X86IntrinsicByte* bytes, uint32_t size)
{
    if (memory->stack)
    {
        X86IntrinsicByte* stack = x86_intrinsic_stack(trace, memory->address, size);
        if (stack != 0)
        {
            memmove(stack, bytes, size * sizeof(*bytes));
        }
    }
    else if (memory->constant)
    {
        x86_intrinsic_fail(trace, "a store into constant data%s", "");
    }
    else if (!trace->failed)
    {
        const uint32_t value = x86_intrinsic_value(trace, bytes, size);
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_appendf(&piece, "store%llu", size);
        x86_intrinsic_append_node(&piece, trace, memory->node);
        x86_intrinsic_append(&piece, "=", 1);
        x86_intrinsic_append_node(&piece, trace, value);
        x86_intrinsic_append(&piece, "\n", 1);
        x86_intrinsic_keep(trace, &piece);
    }
}

// The bytes an operand reads: a register's, or `size` bytes of memory (the operand's own unless given).
static uint32_t x86_intrinsic_read(X86IntrinsicTrace* trace, const X86Decoded* decoded, const X86FormDescription* form, X86IntrinsicPlaceInCode place,
                                   const X86IntrinsicOperand* operand, uint32_t size, X86IntrinsicByte* bytes)
{
    uint32_t node = BUSTER_X86_INTRINSIC_UNKNOWN;
    if (operand->place == X86_INTRINSIC_PLACE_MEMORY)
    {
        const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, size);
        node = trace->failed ? node : x86_intrinsic_load(trace, &memory, size, bytes);
    }
    else
    {
        const X86IntrinsicByte* source = x86_intrinsic_register_bytes(trace, operand);
        if (source != 0)
        {
            memcpy(bytes, source, size * sizeof(*bytes));
            node = x86_intrinsic_value(trace, bytes, size);
        }
        else
        {
            x86_intrinsic_fail(trace, "an operand the trace doesn't follow (an x87, MMX, segment or control register)%s", "");
        }
    }
    return node;
}

// ---------------------------------------------------------------------------------------------------------------------
// The step

typedef enum X86IntrinsicAction
{
    X86_INTRINSIC_NEXT,
    X86_INTRINSIC_CALL,
    X86_INTRINSIC_JUMP,
    // A conditional branch or xbegin, whose two paths the walk follows (X86IntrinsicTrace.branch_condition).
    X86_INTRINSIC_BRANCH,
    X86_INTRINSIC_RETURN,
} X86IntrinsicAction;

static uint64_t x86_intrinsic_rsp(X86IntrinsicTrace* trace)
{
    const uint32_t frame = x86_intrinsic_frame_of(trace, trace->gpr[X86_REGISTER_RSP]);
    if (frame == BUSTER_X86_INTRINSIC_UNKNOWN)
    {
        x86_intrinsic_fail(trace, "RSP isn't a stack address the trace knows%s", "");
    }
    return frame != BUSTER_X86_INTRINSIC_UNKNOWN ? trace->nodes[frame].address : BUSTER_X86_INTRINSIC_ENTRY;
}

static void x86_intrinsic_set_rsp(X86IntrinsicTrace* trace, uint64_t address)
{
    x86_intrinsic_fill(trace->gpr[X86_REGISTER_RSP], 8, x86_intrinsic_frame(trace, address));
}

static void x86_intrinsic_push(X86IntrinsicTrace* trace, const X86IntrinsicByte* bytes)
{
    const uint64_t rsp = x86_intrinsic_rsp(trace) - 8;
    X86IntrinsicByte* stack = trace->failed ? 0 : x86_intrinsic_stack(trace, rsp, 8);
    if (stack != 0)
    {
        memmove(stack, bytes, 8 * sizeof(*bytes));
        x86_intrinsic_set_rsp(trace, rsp);
    }
}

static void x86_intrinsic_pop(X86IntrinsicTrace* trace, X86IntrinsicByte* bytes)
{
    const uint64_t rsp = x86_intrinsic_rsp(trace);
    const X86IntrinsicByte* stack = trace->failed ? 0 : x86_intrinsic_stack(trace, rsp, 8);
    if (stack != 0)
    {
        memcpy(bytes, stack, 8 * sizeof(*bytes));
        x86_intrinsic_set_rsp(trace, rsp + 8);
    }
}

// The kmov forms' bytes, from their mnemonic's last letter (b, w, d or q), or 0 for another mnemonic.
static uint32_t x86_intrinsic_kmov_bytes(const char* mnemonic)
{
    const size_t length = strlen(mnemonic);
    uint32_t result = 0;
    if (length == 5 && strncmp(mnemonic, "kmov", 4) == 0)
    {
        result = mnemonic[4] == 'b' ? 1 : mnemonic[4] == 'w' ? 2 : mnemonic[4] == 'd' ? 4 : mnemonic[4] == 'q' ? 8 : 0;
    }
    return result;
}

// The bytes of an EVEX form's destination elements, which its opmask's bits stand for, from its mnemonic's suffix:
// bf16, ph, phx and sh 2, ps, psx and ss 4, pd and sd 8 (ss, sd and sh but in an integer instruction's, vp...), a lane
// form's bits (32x4), a move's (vmovdqu16), dq and udq 4
// (a conversion's doublewords), qq, uqq and qdq 8, or the last letter's, b, w, d or q; and where the suffix names none,
// a broadcast form's element of its mnemonic.
static uint32_t x86_intrinsic_element(const X86FormDescription* form)
{
    const char* mnemonic = form->mnemonic;
    // AVX10.2's saturating conversions end in an s past their elements' letters (vcvttps2dqs, vcvtph2bf8s).
    const size_t full = strlen(mnemonic);
    const bool saturating = full > 3 && mnemonic[full - 1] == 's' && (mnemonic[full - 2] == 'q' || mnemonic[full - 2] == '8');
    const size_t length = saturating ? full - 1 : full;
    char trimmed[32];
    if (saturating && length < sizeof(trimmed))
    {
        memcpy(trimmed, mnemonic, length);
        trimmed[length] = 0;
        mnemonic = trimmed;
    }
    const char* lanes = strrchr(mnemonic, 'x');
    uint32_t result = 0;
    if (length < 3)
    {
    }
    else if (length >= 4 && strcmp(mnemonic + length - 4, "bf16") == 0)
    {
        result = 2;
    }
    else if (lanes != 0 && lanes > mnemonic + 2 && lanes[1] >= '1' && lanes[1] <= '8' && lanes[2] == 0 && lanes[-1] >= '0' && lanes[-1] <= '9')
    {
        result = lanes[-1] == '2' ? 4 : lanes[-1] == '4' ? 8 : 0;
    }
    else if (mnemonic[length - 1] >= '0' && mnemonic[length - 1] <= '9')
    {
        const char* digits = mnemonic + length;
        while (digits[-1] >= '0' && digits[-1] <= '9')
        {
            digits -= 1;
        }
        result = (uint32_t)atoi(digits) / 8;
    }
    else if (strcmp(mnemonic + length - 3, "qdq") == 0 || strcmp(mnemonic + length - 2, "qq") == 0)
    {
        result = 8;
    }
    else if (strcmp(mnemonic + length - 2, "dq") == 0)
    {
        result = 4;
    }
    else
    {
        // A trailing x (vcvtps2phx, vcvtph2psx) names the encoding, not the elements; an integer instruction's (vp...)
        // ss, sd and sh aren't scalars' (vpdpbusd's signed doublewords).
        const size_t end = mnemonic[length - 1] == 'x' ? length - 1 : length;
        const char* suffix = mnemonic + end - 2;
        const char last = mnemonic[end - 1];
        const bool scalar = strncmp(mnemonic, "vp", 2) != 0;
        result = strncmp(suffix, "ps", 2) == 0 || (scalar && strncmp(suffix, "ss", 2) == 0)   ? 4
                 : strncmp(suffix, "pd", 2) == 0 || (scalar && strncmp(suffix, "sd", 2) == 0) ? 8
                 : strncmp(suffix, "ph", 2) == 0 || (scalar && strncmp(suffix, "sh", 2) == 0) ? 2
                 : last == 'b'                                                     ? 1
                 : last == 'w'                                                     ? 2
                 : last == 'd'                                                     ? 4
                 : last == 'q'                                                     ? 8
                                                                                   : 0;
    }
    for (uint32_t row = 1; result == 0 && row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* other = &x86_form_descriptions[row];
        const bool broadcast = other->family == BUSTER_X86_FAMILY_EVEX && (other->evex & BUSTER_X86_EVEX_BROADCAST) != 0;
        result = broadcast && strcmp(other->mnemonic, form->mnemonic) == 0 ? other->disp8_scale : 0;
    }
    return result;
}

// The lanes of a masked form of `size` bytes' destination: one per element, or 64 where its elements are unknown, but no
// more than a conversion's source elements (its rm operand's bytes over its broadcast's element), whose others it zeroes:
// vcvtpd2dq's two doublewords of an XMM register's doubles, vcvtph2bf8's eight bytes of its halves (a conversion of two
// vectors into one, vcvt2ps2phx's, has twice its rm's); and the bytes of the opmask they read, at least one.
static uint32_t x86_intrinsic_lanes(const X86FormDescription* form, uint32_t size)
{
    const uint32_t element = x86_intrinsic_element(form);
    uint32_t result = element != 0 && size >= element ? size / element : 64;
    const uint8_t operation = x86_effects((uint16_t)(form - x86_form_descriptions))->operation;
    const bool conversion = (operation == X86_OPERATION_FLOAT_TO_INT || operation == X86_OPERATION_FLOAT_TO_FLOAT || operation == X86_OPERATION_INT_TO_FLOAT) &&
                            strncmp(form->mnemonic, "vcvt2", 5) != 0;
    for (uint32_t row = 1; conversion && row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* other = &x86_form_descriptions[row];
        const bool broadcast = other->family == BUSTER_X86_FAMILY_EVEX && (other->evex & BUSTER_X86_EVEX_BROADCAST) != 0;
        if (broadcast && strcmp(other->mnemonic, form->mnemonic) == 0 && other->disp8_scale != 0 && form->operand_bytes / other->disp8_scale < result)
        {
            result = form->operand_bytes / other->disp8_scale;
        }
    }
    return result;
}

static uint32_t x86_intrinsic_mask_bytes(uint32_t lanes)
{
    return lanes <= 8 ? 1 : (lanes + 7) / 8;
}

// A masked result's node: select<element>(mask, value, else), where `zero` makes else 0; or the value itself where the
// mask is a constant with every lane's bit set, and else where it has none.
static uint32_t x86_intrinsic_select(X86IntrinsicTrace* trace, const X86FormDescription* form, uint32_t mask, uint32_t lanes, uint32_t value, bool zero,
                                     uint32_t otherwise, uint32_t bytes)
{
    const uint32_t mask_node = x86_intrinsic_value(trace, trace->opmask[mask], x86_intrinsic_mask_bytes(lanes));
    const X86IntrinsicNode* node = &trace->nodes[mask_node];
    uint32_t result = BUSTER_X86_INTRINSIC_UNKNOWN;
    bool all = node->kind == X86_INTRINSIC_CONSTANT;
    bool none = node->kind == X86_INTRINSIC_CONSTANT;
    for (uint32_t lane = 0; (all || none) && lane < lanes && lane < 64; lane += 1)
    {
        const bool set = (node->constant[lane / 8] >> (lane % 8) & 1) != 0;
        all = all && set;
        none = none && !set;
    }
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    if (all)
    {
        result = value;
    }
    else if (none && !zero)
    {
        result = otherwise;
    }
    else if (none)
    {
        static const uint8_t zeros[64] = { 0 };
        result = x86_intrinsic_constant(trace, zeros, bytes <= 64 ? bytes : 64);
    }
    else
    {
        x86_intrinsic_appendf(&piece, "select%llu(", x86_intrinsic_element(form));
        x86_intrinsic_append_node(&piece, trace, mask_node);
        x86_intrinsic_append(&piece, ",", 1);
        x86_intrinsic_append_node(&piece, trace, value);
        x86_intrinsic_append(&piece, ",", 1);
        if (zero)
        {
            x86_intrinsic_append(&piece, "0", 1);
        }
        else
        {
            x86_intrinsic_append_node(&piece, trace, otherwise);
        }
        x86_intrinsic_append(&piece, ")", 1);
        result = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, bytes, &piece);
    }
    return result;
}

// The integer operations whose result's first bytes are the operation's of their operands' first bytes, which
// x86_intrinsic_alu gives a node of their own, without the form: an 8-bit and and a 32-bit one of the same low byte have
// the same first byte.
static const char* x86_intrinsic_alu_name(uint32_t operation)
{
    const char* name = 0;
    switch (operation)
    {
    case X86_OPERATION_ADD: name = "add"; break;
    case X86_OPERATION_OR: name = "or"; break;
    case X86_OPERATION_AND: name = "and"; break;
    case X86_OPERATION_SUB: name = "sub"; break;
    case X86_OPERATION_XOR: name = "xor"; break;
    case X86_OPERATION_NOT: name = "not"; break;
    case X86_OPERATION_NEG: name = "neg"; break;
    case X86_OPERATION_SHL: name = "shl"; break;
    case X86_OPERATION_IMUL: name = "imul"; break;
    default: break;
    }
    return name;
}

// An integer operation's node of `bytes` bytes, of its operands' nodes: its text name<bytes>(operands), a commutative
// one's (add, and, or, xor, imul) in their texts' order; an and or an or of booleans is one.
static uint32_t x86_intrinsic_combine(X86IntrinsicTrace* trace, const char* name, uint32_t bytes, uint32_t* values, uint32_t count)
{
    const bool commutative = strcmp(name, "add") == 0 || strcmp(name, "and") == 0 || strcmp(name, "or") == 0 || strcmp(name, "xor") == 0 || strcmp(name, "imul") == 0;
    if (commutative && count == 2 && !trace->failed)
    {
        const X86IntrinsicNode* a = &trace->nodes[values[0]];
        const X86IntrinsicNode* b = &trace->nodes[values[1]];
        const uint32_t shorter = a->length < b->length ? a->length : b->length;
        const int order = memcmp(trace->text + a->text, trace->text + b->text, shorter);
        if (order > 0 || (order == 0 && a->length > b->length))
        {
            const uint32_t first = values[1];
            values[1] = values[0];
            values[0] = first;
        }
    }
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    x86_intrinsic_append(&piece, name, (uint32_t)strlen(name));
    x86_intrinsic_appendf(&piece, "%llu(", bytes);
    bool booleans = count == 2 && bytes == 1 && (strcmp(name, "and") == 0 || strcmp(name, "or") == 0);
    for (uint32_t o = 0; o < count; o += 1)
    {
        x86_intrinsic_append(&piece, o != 0 ? "," : "", o != 0 ? 1 : 0);
        x86_intrinsic_append_node(&piece, trace, values[o]);
        booleans = booleans && trace->nodes[values[o]].boolean;
    }
    x86_intrinsic_append(&piece, ")", 1);
    const uint32_t node = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, bytes, &piece);
    trace->nodes[node].boolean = booleans;
    return node;
}

// An integer operation's node of `bytes` bytes: its text name<bytes>(operands), each operand's first `bytes` bytes (to 8,
// or a constant's).
static uint32_t x86_intrinsic_alu_node(X86IntrinsicTrace* trace, const char* name, uint32_t bytes, X86IntrinsicByte operands[2][8], uint32_t count)
{
    uint32_t values[2] = { 0 };
    for (uint32_t o = 0; o < count; o += 1)
    {
        values[o] = x86_intrinsic_value(trace, operands[o], bytes);
    }
    return x86_intrinsic_combine(trace, name, bytes, values, count);
}

// An integer operation's result of `width` bytes, with its results of its first 1, 2 and 4 bytes, each made of its
// operands' first bytes, which a value of its first bytes is (x86_intrinsic_value). They are made first, of operands
// that are earlier nodes, so that no value's making waits on another's.
static uint32_t x86_intrinsic_alu(X86IntrinsicTrace* trace, const char* name, uint32_t width, X86IntrinsicByte operands[2][8], uint32_t count)
{
    static const uint32_t narrower[3] = { 1, 2, 4 };
    uint32_t narrowed[3] = { 0 };
    for (uint32_t n = 0; n < 3 && !trace->failed; n += 1)
    {
        narrowed[n] = narrower[n] < width ? x86_intrinsic_alu_node(trace, name, narrower[n], operands, count) : 0;
    }
    const uint32_t result = trace->failed ? BUSTER_X86_INTRINSIC_UNKNOWN : x86_intrinsic_alu_node(trace, name, width, operands, count);
    for (uint32_t n = 0; n < 3 && !trace->failed; n += 1)
    {
        trace->nodes[result].narrow[n] = narrowed[n];
    }
    return result;
}

// A condition code's number by its name in a setcc's or a cmovcc's mnemonic (o, no, b, ae, e, ne, be, a, s, ns, p, np, l,
// ge, le, g: the number's low bit negates it), or -1.
static int32_t x86_intrinsic_condition(const char* name)
{
    static const char* const names[16] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
    int32_t result = -1;
    for (int32_t c = 0; result < 0 && c < 16; c += 1)
    {
        result = strcmp(name, names[c]) == 0 ? c : -1;
    }
    return result;
}

// The boolean of condition `condition` of the flags `flags`: set<cc>(flags).
static uint32_t x86_intrinsic_set_of(X86IntrinsicTrace* trace, int32_t condition, uint32_t flags)
{
    static const char* const names[16] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
    static X86IntrinsicPiece piece;
    piece.length = 0;
    piece.full = false;
    x86_intrinsic_append(&piece, "set", 3);
    x86_intrinsic_append(&piece, names[condition & 15], (uint32_t)strlen(names[condition & 15]));
    x86_intrinsic_append(&piece, "(", 1);
    x86_intrinsic_append_node(&piece, trace, flags);
    x86_intrinsic_append(&piece, ")", 1);
    const uint32_t node = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 1, &piece);
    if (!trace->failed)
    {
        trace->nodes[node].boolean = true;
        trace->nodes[node].condition = (uint8_t)((condition & 15) + 1);
        trace->nodes[node].tested = flags;
    }
    return node;
}

// The same of the trace's flags.
static uint32_t x86_intrinsic_set(X86IntrinsicTrace* trace, int32_t condition)
{
    return x86_intrinsic_set_of(trace, condition, trace->flags);
}

// Whether `count` bytes are a boolean's, zero-extended, and its node.
static bool x86_intrinsic_boolean(const X86IntrinsicTrace* trace, const X86IntrinsicByte* bytes, uint32_t count, uint32_t* node)
{
    bool result = count != 0 && bytes[0].node > BUSTER_X86_INTRINSIC_ZERO && bytes[0].index == 0 && trace->nodes[bytes[0].node].boolean;
    for (uint32_t i = 1; result && i < count; i += 1)
    {
        result = bytes[i].node == BUSTER_X86_INTRINSIC_ZERO;
    }
    *node = bytes[0].node;
    return result;
}

// Whether `count` bytes are a constant of the value `value` (to 255).
static bool x86_intrinsic_small(const X86IntrinsicTrace* trace, const X86IntrinsicByte* bytes, uint32_t count, uint8_t value)
{
    bool result = count != 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        uint8_t byte = 0;
        result = x86_intrinsic_constant_byte(trace, bytes[i], &byte) && byte == (i == 0 ? value : 0);
    }
    return result;
}

// Whether an EVEX form is an integer compare into an opmask: VPCMPEQ and VPCMPGT (predicates 0 and 6) and VPCMP and
// VPCMPU (the immediate's low 3 bits), with its elements' letter.
static bool x86_intrinsic_compare(const X86FormDescription* form, int32_t immediate, uint32_t* predicate, char* letter, bool* is_unsigned)
{
    const char* mnemonic = form->mnemonic;
    const size_t length = strlen(mnemonic);
    const char last = length != 0 ? mnemonic[length - 1] : 0;
    const bool element = last == 'b' || last == 'w' || last == 'd' || last == 'q';
    const bool fixed = length == 8 && (strncmp(mnemonic, "vpcmpeq", 7) == 0 || strncmp(mnemonic, "vpcmpgt", 7) == 0);
    const bool chosen = (length == 6 && strncmp(mnemonic, "vpcmp", 5) == 0) || (length == 7 && strncmp(mnemonic, "vpcmpu", 6) == 0);
    const bool result = form->family == BUSTER_X86_FAMILY_EVEX && element && (fixed || chosen);
    *predicate = fixed ? (mnemonic[6] == 'q' ? 0 : 6) : (uint32_t)immediate & 7;
    *letter = last;
    *is_unsigned = chosen && length == 7;
    return result;
}

// A conditional move of a constant 1 or 0 (the source) over a constant 0 or a boolean (the destination), as Tier 0's rows
// make a condition's int (check_x86_lower_condition): set<cc>, or(set<cc>, old) where it moves 1, and(set<!cc>, old)
// where it moves 0, the boolean zero-extended into the register, as clang's setcc, and and or of them, and movzx give it.
// Returns whether the move is one.
static bool x86_intrinsic_cmov_boolean(X86IntrinsicTrace* trace, const X86Decoded* decoded, const X86FormDescription* form, X86IntrinsicPlaceInCode place,
                                       const X86IntrinsicOperand* operands, uint32_t written)
{
    static X86IntrinsicByte old[8];
    static X86IntrinsicByte moved[8];
    const uint32_t width = operands[written].bytes;
    memcpy(old, x86_intrinsic_register_bytes(trace, &operands[written]), width * sizeof(*old));
    x86_intrinsic_read(trace, decoded, form, place, &operands[BUSTER_X86_ROLE_RM], width, moved);
    const int32_t condition = x86_intrinsic_condition(form->mnemonic + 4);
    uint32_t boolean = BUSTER_X86_INTRINSIC_UNKNOWN;
    const bool zero = x86_intrinsic_small(trace, old, width, 0);
    const bool prior = zero || x86_intrinsic_boolean(trace, old, width, &boolean);
    const bool one = x86_intrinsic_small(trace, moved, width, 1);
    const bool none = x86_intrinsic_small(trace, moved, width, 0);
    const bool result = !trace->failed && prior && (one || none);
    if (result)
    {
        uint32_t node = BUSTER_X86_INTRINSIC_ZERO;
        if (one && zero)
        {
            node = x86_intrinsic_set(trace, condition);
        }
        else if (!zero)
        {
            uint32_t values[2] = { x86_intrinsic_set(trace, one ? condition : condition ^ 1), boolean };
            node = x86_intrinsic_combine(trace, one ? "or" : "and", 1, values, 2);
        }
        x86_intrinsic_fill(old, 1, node);
        x86_intrinsic_zero(&old[1], 7);
        x86_intrinsic_write(trace, &operands[written], old, width, BUSTER_X86_REST_ZERO, false);
    }
    return result;
}

// The bytes of an implicit general register a form reads: UMWAIT's, TPAUSE's and LOADIWKEY's EDX:EAX and EAX are 32-bit
// halves, whose upper bits a compiler's code may leave as anything; every other form's, the whole register.
static uint32_t x86_intrinsic_implicit_bytes(uint32_t number)
{
    return number == X86_FORM_UMWAIT_R32 || number == X86_FORM_TPAUSE_R32 || number == X86_FORM_LOADIWKEY_X_X ? 4 : 8;
}

// The boolean whose negation a test or a compare gives ZF: a general register's or the accumulator's boolean (a
// condition's set<cc>, zero-extended), tested with 1 or with itself, or compared with 0 (clang's sete, then test $1,
// before its branch), or 0.
static uint32_t x86_intrinsic_tested(X86IntrinsicTrace* trace, const X86Decoded* decoded, const X86FormDescription* form, const X86Effects* effects,
                                     const X86IntrinsicOperand* operands)
{
    const X86Instruction* instruction = &decoded->instruction;
    const uint32_t number = instruction->form & BUSTER_X86_FORM_NUMBER;
    const X86IntrinsicOperand* rm = &operands[BUSTER_X86_ROLE_RM];
    const bool accumulator = rm->place == X86_INTRINSIC_PLACE_NONE && (effects->implicit_reads & BUSTER_X86_GPR(RAX)) != 0;
    const X86IntrinsicByte* bytes = rm->place == X86_INTRINSIC_PLACE_GPR ? x86_intrinsic_register_bytes(trace, rm) : accumulator ? trace->gpr[X86_REGISTER_RAX] : 0;
    const uint32_t width = rm->place == X86_INTRINSIC_PLACE_GPR ? rm->bytes : form->operand_bytes;
    const uint64_t immediate = form->immediate_bytes != 0 ? x86_immediate_value(number, instruction->immediate, instruction->displacement) : 0;
    const bool itself = effects->operation == X86_OPERATION_TEST && form->immediate_bytes == 0 && operands[BUSTER_X86_ROLE_REG].place == X86_INTRINSIC_PLACE_GPR &&
                        rm->place == X86_INTRINSIC_PLACE_GPR && instruction->reg == instruction->rm;
    const bool one = effects->operation == X86_OPERATION_TEST && form->immediate_bytes != 0 && immediate == 1;
    const bool zero = effects->operation == X86_OPERATION_CMP && form->immediate_bytes != 0 && immediate == 0;
    uint32_t boolean = 0;
    const bool found = bytes != 0 && width != 0 && width <= 8 && (itself || one || zero) && x86_intrinsic_boolean(trace, bytes, width, &boolean);
    return found ? boolean : 0;
}

// A conditional jump's condition (cc on the flags), as X86IntrinsicTrace.branch_condition and branch_holds give it for
// the jump's taken path: the node set<c>(flags) of the condition c of the pair (the even one: e for e and ne), which
// holds on the taken path where cc is c. Where the flags are a boolean's test (flags_boolean), ZF is the boolean's
// negation, and a jump on e or ne the boolean's own condition, as its node gives it.
static void x86_intrinsic_branch_condition(X86IntrinsicTrace* trace, int32_t condition)
{
    const X86IntrinsicNode* boolean = trace->flags_boolean != 0 ? &trace->nodes[trace->flags_boolean] : 0;
    const bool zero_flag = condition == 4 || condition == 5;
    if (boolean != 0 && zero_flag && boolean->condition != 0)
    {
        // Taken on e where the boolean is 0, on ne where it is 1; the boolean set<b>(F) is 1 where its pair's even
        // condition is b's own (b even) and 0 where it is b's negation (b odd).
        const int32_t tested = boolean->condition - 1;
        const bool boolean_taken = condition == 5;
        trace->branch_condition = x86_intrinsic_set_of(trace, tested & ~1, boolean->tested);
        trace->branch_holds = boolean_taken == ((tested & 1) == 0);
    }
    else if (boolean != 0 && zero_flag)
    {
        x86_intrinsic_fail(trace, "a branch on a boolean that isn't a condition's (an and or an or of them)%s", "");
    }
    else
    {
        trace->branch_condition = x86_intrinsic_set_of(trace, condition & ~1, trace->flags);
        trace->branch_holds = (condition & 1) == 0;
    }
    trace->branch_abort = false;
}

// One record, at `place` in the object's code: what it does to the trace's state, and where control goes after it
// (the next record, a call's or a jump's target, `target`, a branch's two paths, or a return).
static X86IntrinsicAction x86_intrinsic_step(X86IntrinsicTrace* trace, const X86Decoded* decoded, X86IntrinsicPlaceInCode place, uint64_t* target)
{
    X86IntrinsicAction action = X86_INTRINSIC_NEXT;
    const X86Instruction* instruction = &decoded->instruction;
    const uint32_t number = instruction->form & BUSTER_X86_FORM_NUMBER;
    const X86FormDescription* form = x86_form_description(number);
    const X86Effects* effects = x86_effects(number);
    const uint32_t operation = effects->operation;
    const uint32_t control = x86_effects_control(effects);
    X86IntrinsicOperand operands[BUSTER_X86_INTRINSIC_ROLES];
    x86_intrinsic_operands(instruction, form, effects, operands);
    const uint32_t mask = instruction->decorations & BUSTER_X86_DECORATION_MASK;
    const bool masked = form->family == BUSTER_X86_FAMILY_EVEX && mask != 0 && (form->evex & BUSTER_X86_EVEX_MASK) != 0;
    const bool zeroing = masked && (instruction->decorations & BUSTER_X86_DECORATION_ZEROING) != 0;
    // The written role and the read one, of a form of two operands (a move's): the first syntax operand and the other.
    int32_t written = -1;
    int32_t source = -1;
    for (uint32_t role = 0; role < 3; role += 1)
    {
        written = written < 0 && operands[role].write ? (int32_t)role : written;
    }
    for (uint32_t role = 0; role < 3; role += 1)
    {
        source = source < 0 && operands[role].read && (int32_t)role != written ? (int32_t)role : source;
    }
    static X86IntrinsicByte bytes[64];
    static X86IntrinsicByte other[64];
    const uint32_t kmov = x86_intrinsic_kmov_bytes(form->mnemonic);
    const bool blend = form->family == BUSTER_X86_FAMILY_EVEX && (strncmp(form->mnemonic, "vpblendm", 8) == 0 || strncmp(form->mnemonic, "vblendm", 7) == 0);
    uint32_t predicate = 0;
    char letter = 0;
    bool unsigned_compare = false;
    const bool select = form->family == BUSTER_X86_FAMILY_EVEX && written >= 0 && ((operation == X86_OPERATION_VECTOR_MOVE && masked && source >= 0) || blend);
    if (operation == X86_OPERATION_NONE)
    {
        x86_intrinsic_fail(trace, "%s, a form without effects in x86_semantics.c", form->mnemonic);
        bool known = false;
        for (uint32_t i = 0; i < x86_intrinsic_uncovered_count; i += 1)
        {
            known = known || x86_intrinsic_uncovered[i] == number;
        }
        if (!known && x86_intrinsic_uncovered_count < BUSTER_ARRAY_LENGTH(x86_intrinsic_uncovered))
        {
            x86_intrinsic_uncovered[x86_intrinsic_uncovered_count++] = (uint16_t)number;
        }
    }
    else if (control == BUSTER_X86_CONTROL_RETURN)
    {
        x86_intrinsic_pop(trace, bytes);
        action = X86_INTRINSIC_RETURN;
    }
    else if (control == BUSTER_X86_CONTROL_CALL && form->relative_bytes == 4)
    {
        // A direct call: its target is the relocation's, or the displacement's within the section.
        const Elf64_Rela* relocation = x86_intrinsic_relocation(trace->object, place.section, place.offset + place.length - 4);
        uint32_t section = place.section;
        uint64_t offset = place.offset + place.length + (uint64_t)(int64_t)instruction->displacement;
        const bool found = relocation != 0 ? x86_intrinsic_relocation_target(trace->object, relocation, 0, &section, &offset) : true;
        if (found && section == place.section)
        {
            static X86IntrinsicByte address[8];
            x86_intrinsic_fill(address, 8, x86_intrinsic_named(trace, X86_INTRINSIC_VALUE, 8, "ra"));
            x86_intrinsic_push(trace, address);
            *target = offset;
            action = X86_INTRINSIC_CALL;
        }
        else
        {
            x86_intrinsic_fail(trace, "a call of a function outside the object's code%s", "");
        }
    }
    else if (control == BUSTER_X86_CONTROL_JUMP && form->relative_bytes != 0)
    {
        *target = place.offset + place.length + (uint64_t)(int64_t)instruction->displacement;
        action = X86_INTRINSIC_JUMP;
    }
    else if (control == BUSTER_X86_CONTROL_BRANCH && form->relative_bytes != 0 && operation == X86_OPERATION_JCC && x86_intrinsic_condition(form->mnemonic + 1) >= 0)
    {
        // A conditional jump: its two paths, and its condition (x86_intrinsic_branch_condition).
        x86_intrinsic_branch_condition(trace, x86_intrinsic_condition(form->mnemonic + 1));
        *target = place.offset + place.length + (uint64_t)(int64_t)instruction->displacement;
        action = X86_INTRINSIC_BRANCH;
    }
    else if (control == BUSTER_X86_CONTROL_BRANCH && form->relative_bytes != 0 && number == X86_FORM_XBEGIN_REL32)
    {
        // xbegin: the transaction runs on, or aborts to the fallback, the branch's target, its status in EAX; its node
        // holds where it aborts.
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_append(&piece, form->mnemonic, (uint32_t)strlen(form->mnemonic));
        x86_intrinsic_appendf(&piece, "#%llu()", x86_intrinsic_canonical[number]);
        trace->branch_condition = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 0, &piece);
        trace->branch_holds = true;
        trace->branch_abort = true;
        *target = place.offset + place.length + (uint64_t)(int64_t)instruction->displacement;
        action = X86_INTRINSIC_BRANCH;
    }
    else if (control != BUSTER_X86_CONTROL_NONE)
    {
        x86_intrinsic_fail(trace, "%s, control the trace doesn't follow (a branch, a trap or an indirect transfer)", form->mnemonic);
    }
    else if (operation == X86_OPERATION_NOP || operation == X86_OPERATION_ENDBR)
    {
    }
    else if (operation == X86_OPERATION_PREFETCH && operands[BUSTER_X86_ROLE_RM].place == X86_INTRINSIC_PLACE_MEMORY)
    {
        // A prefetch's or a cache line's hint, which reads nothing: an effect of its form and its address.
        const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, 0);
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_append(&piece, form->mnemonic, (uint32_t)strlen(form->mnemonic));
        x86_intrinsic_appendf(&piece, "#%llu(", x86_intrinsic_canonical[number]);
        x86_intrinsic_append_node(&piece, trace, memory.stack ? x86_intrinsic_frame(trace, memory.address) : memory.node);
        x86_intrinsic_append(&piece, ")", 1);
        x86_intrinsic_effect(trace, trace->failed ? BUSTER_X86_INTRINSIC_UNKNOWN : x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 0, &piece));
    }
    else if (operation == X86_OPERATION_PUSH)
    {
        if (operands[BUSTER_X86_ROLE_RM].place == X86_INTRINSIC_PLACE_GPR && operands[BUSTER_X86_ROLE_RM].bytes == 8)
        {
            memcpy(bytes, trace->gpr[operands[BUSTER_X86_ROLE_RM].number], sizeof(trace->gpr[0]));
            x86_intrinsic_push(trace, bytes);
        }
        else
        {
            x86_intrinsic_fail(trace, "%s of other than a 64-bit register", form->mnemonic);
        }
    }
    else if (operation == X86_OPERATION_POP)
    {
        if (operands[BUSTER_X86_ROLE_RM].place == X86_INTRINSIC_PLACE_GPR && operands[BUSTER_X86_ROLE_RM].bytes == 8)
        {
            x86_intrinsic_pop(trace, bytes);
            memcpy(trace->gpr[operands[BUSTER_X86_ROLE_RM].number], bytes, sizeof(trace->gpr[0]));
        }
        else
        {
            x86_intrinsic_fail(trace, "%s of other than a 64-bit register", form->mnemonic);
        }
    }
    else if (operation == X86_OPERATION_LEAVE)
    {
        memcpy(trace->gpr[X86_REGISTER_RSP], trace->gpr[X86_REGISTER_RBP], sizeof(trace->gpr[0]));
        x86_intrinsic_pop(trace, bytes);
        memcpy(trace->gpr[X86_REGISTER_RBP], bytes, sizeof(trace->gpr[0]));
    }
    else if (operation == X86_OPERATION_ZERO_UPPER)
    {
        for (uint32_t r = 0; r < 16; r += 1)
        {
            x86_intrinsic_zero(&trace->vector[r][16], 48);
        }
    }
    else if (operation == X86_OPERATION_LEA && written >= 0)
    {
        const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, 0);
        const uint32_t node = memory.stack ? x86_intrinsic_frame(trace, memory.address) : memory.node;
        x86_intrinsic_fill(bytes, 8, node);
        x86_intrinsic_write(trace, &operands[written], bytes, operands[written].bytes, BUSTER_X86_REST_ZERO, false);
    }
    else if ((operation == X86_OPERATION_ADD || operation == X86_OPERATION_SUB || operation == X86_OPERATION_AND) && written >= 0 &&
             operands[written].place == X86_INTRINSIC_PLACE_GPR && operands[written].bytes == 8 && form->immediate_bytes != 0 &&
             x86_intrinsic_frame_of(trace, trace->gpr[operands[written].number]) != BUSTER_X86_INTRINSIC_UNKNOWN)
    {
        // Arithmetic on a stack address: RSP's reservation and realignment, an address's offset.
        const uint32_t frame = x86_intrinsic_frame_of(trace, trace->gpr[operands[written].number]);
        const uint64_t address = trace->nodes[frame].address;
        const uint64_t value = x86_immediate_value(number, instruction->immediate, instruction->displacement);
        const uint64_t result = operation == X86_OPERATION_ADD ? address + value : operation == X86_OPERATION_SUB ? address - value : address & value;
        x86_intrinsic_fill(trace->gpr[operands[written].number], 8, x86_intrinsic_frame(trace, result));
        trace->flags = BUSTER_X86_INTRINSIC_UNKNOWN;
        trace->flags_boolean = 0;
    }
    else if ((effects->properties & BUSTER_X86_EFFECT_ZERO_IDIOM) != 0 && written >= 0 && !masked &&
             ((form->family == BUSTER_X86_FAMILY_LEGACY && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->reg == instruction->rm) ||
              (form->family != BUSTER_X86_FAMILY_LEGACY && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->vvvv == instruction->rm)))
    {
        x86_intrinsic_zero(bytes, 64);
        const uint32_t count = effects->write_bytes != 0 ? effects->write_bytes : operands[written].bytes;
        x86_intrinsic_write(trace, &operands[written], bytes, count, x86_effects_role(effects, (uint32_t)written) >> BUSTER_X86_ROLE_REST_SHIFT & 3,
                            (effects->properties & BUSTER_X86_EFFECT_KEEP_UPPER) != 0);
        // A general register's (xor eax, eax) defines the flags; a vector register's (pxor, vpxor) leaves them.
        if (effects->flags_written != 0 || effects->flags_undefined != 0)
        {
            trace->flags = x86_intrinsic_named(trace, X86_INTRINSIC_VALUE, 0, "zero-flags");
            trace->flags_boolean = 0;
        }
    }
    else if (select)
    {
        // A masked move's source, or a blend's rm, where the mask's bits are set, and elsewhere the destination's old
        // value (a blend's vvvv), or 0 where it zeroes, as select<element bytes>(mask, value, else), whichever form
        // gives it: a blend under no mask is its rm.
        const X86IntrinsicOperand* to = &operands[written];
        const uint32_t size = to->place == X86_INTRINSIC_PLACE_MEMORY ? form->operand_bytes : to->bytes;
        const uint32_t value = x86_intrinsic_read(trace, decoded, form, place, &operands[blend ? BUSTER_X86_ROLE_RM : (uint32_t)source], size, bytes);
        uint32_t result = value;
        if (masked && !trace->failed)
        {
            const uint32_t old = zeroing ? BUSTER_X86_INTRINSIC_UNKNOWN
                                         : x86_intrinsic_read(trace, decoded, form, place, blend ? &operands[BUSTER_X86_ROLE_VVVV] : to, size, other);
            result = trace->failed ? result : x86_intrinsic_select(trace, form, mask, x86_intrinsic_lanes(form, size), value, zeroing, old, size);
        }
        x86_intrinsic_fill(bytes, size, result);
        if (!trace->failed && to->place == X86_INTRINSIC_PLACE_MEMORY)
        {
            const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, size);
            if (!trace->failed)
            {
                x86_intrinsic_store(trace, &memory, bytes, size);
            }
        }
        else if (!trace->failed)
        {
            x86_intrinsic_write(trace, to, bytes, size, BUSTER_X86_REST_ZERO, false);
        }
    }
    else if (!masked && written >= 0 &&
             ((operation == X86_OPERATION_MOV && (source >= 0 || form->immediate_bytes != 0)) || operation == X86_OPERATION_MOVZX ||
              operation == X86_OPERATION_MOVSX || operation == X86_OPERATION_MOVD || operation == X86_OPERATION_VECTOR_MOVE ||
              (operation == X86_OPERATION_FMOVE && source >= 0 && (operands[source].place == X86_INTRINSIC_PLACE_MEMORY ||
                                                                   operands[written].place == X86_INTRINSIC_PLACE_MEMORY)) ||
              (operation == X86_OPERATION_OPMASK && kmov != 0 && source >= 0)))
    {
        // A move: the source's bytes (an immediate's, sign-extended), as many as the narrower operand has, extended
        // with zeros (movzx, movd and movq, a load of movss or movsd, kmov) or the sign (movsx).
        const X86IntrinsicOperand* to = &operands[written];
        const X86IntrinsicOperand* from = source >= 0 ? &operands[source] : 0;
        uint32_t count = kmov != 0 ? kmov : to->place == X86_INTRINSIC_PLACE_MEMORY ? form->operand_bytes : to->bytes;
        if (from != 0 && from->place != X86_INTRINSIC_PLACE_MEMORY && from->bytes < count && kmov == 0)
        {
            count = from->bytes;
        }
        if (from != 0 && from->place == X86_INTRINSIC_PLACE_MEMORY && form->operand_bytes < count)
        {
            count = form->operand_bytes;
        }
        if (from == 0)
        {
            uint8_t value[8];
            const uint64_t immediate = x86_immediate_value(number, instruction->immediate, instruction->displacement);
            memcpy(value, &immediate, sizeof(value));
            x86_intrinsic_fill(bytes, count, x86_intrinsic_constant(trace, value, count));
        }
        else
        {
            x86_intrinsic_read(trace, decoded, form, place, from, count, bytes);
        }
        uint32_t extended = count;
        const uint32_t room = to->place == X86_INTRINSIC_PLACE_MEMORY ? count : to->bytes;
        uint8_t constant[8] = { 0 };
        bool constants = count <= 8;
        for (uint32_t i = 0; constants && i < count; i += 1)
        {
            constants = x86_intrinsic_constant_byte(trace, bytes[i], &constant[i]);
        }
        if (operation == X86_OPERATION_MOVSX && count < room && constants && room <= 8)
        {
            // A constant's sign extension is a constant.
            memset(constant + count, (constant[count - 1] & 0x80) != 0 ? 0xff : 0, room - count);
            x86_intrinsic_fill(bytes, room, x86_intrinsic_constant(trace, constant, room));
            extended = room;
        }
        else if (operation == X86_OPERATION_MOVSX && count < room)
        {
            static X86IntrinsicPiece piece;
            piece.length = 0;
            piece.full = false;
            x86_intrinsic_append(&piece, "sx(", 3);
            x86_intrinsic_append_node(&piece, trace, x86_intrinsic_value(trace, bytes, count));
            x86_intrinsic_append(&piece, ")", 1);
            const uint32_t sign = x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, room, &piece);
            for (uint32_t i = count; i < room; i += 1)
            {
                bytes[i] = (X86IntrinsicByte){ .node = sign, .index = i };
            }
            extended = room;
        }
        else if (count < room)
        {
            x86_intrinsic_zero(&bytes[count], room - count);
            extended = room;
        }
        if (!trace->failed && to->place == X86_INTRINSIC_PLACE_MEMORY)
        {
            const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, count);
            if (!trace->failed)
            {
                x86_intrinsic_store(trace, &memory, bytes, count);
            }
        }
        else if (!trace->failed)
        {
            const uint32_t rest = operation == X86_OPERATION_VECTOR_MOVE ? x86_effects_role(effects, (uint32_t)written) >> BUSTER_X86_ROLE_REST_SHIFT & 3
                                                                       : BUSTER_X86_REST_ZERO;
            x86_intrinsic_write(trace, to, bytes, extended, rest, (effects->properties & BUSTER_X86_EFFECT_KEEP_UPPER) != 0);
        }
    }
    else if (x86_intrinsic_alu_name(operation) != 0 && written >= 0 && !masked &&
             (operands[written].place == X86_INTRINSIC_PLACE_GPR || operands[written].place == X86_INTRINSIC_PLACE_MEMORY) && effects->implicit_reads == 0 &&
             effects->implicit_writes == 0 && (operation != X86_OPERATION_SHL || form->immediate_bytes != 0))
    {
        // An integer operation of a general register or memory: its operands are the destination's old value where it
        // reads it, its source and its immediate (as the form extends it), each of the destination's bytes.
        const X86IntrinsicOperand* to = &operands[written];
        const uint32_t width = to->place == X86_INTRINSIC_PLACE_MEMORY ? form->operand_bytes : to->bytes;
        static X86IntrinsicByte values[2][8];
        uint32_t count = 0;
        for (uint32_t role = 0; role < 3 && count < 2 && !trace->failed && width <= 8; role += 1)
        {
            if (operands[role].read)
            {
                const uint32_t size = operands[role].place == X86_INTRINSIC_PLACE_MEMORY ? form->operand_bytes : operands[role].bytes;
                x86_intrinsic_read(trace, decoded, form, place, &operands[role], size < width ? size : width, values[count]);
                x86_intrinsic_zero(&values[count][size < width ? size : width], 8 - (size < width ? size : width));
                count += 1;
            }
        }
        if (form->immediate_bytes != 0 && count < 2 && width <= 8)
        {
            uint8_t value[8];
            const uint64_t immediate = x86_immediate_value(number, instruction->immediate, instruction->displacement);
            memcpy(value, &immediate, sizeof(value));
            x86_intrinsic_fill(values[count], 8, x86_intrinsic_constant(trace, value, 8));
            count += 1;
        }
        const uint32_t result = width <= 8 && !trace->failed ? x86_intrinsic_alu(trace, x86_intrinsic_alu_name(operation), width, values, count)
                                                             : BUSTER_X86_INTRINSIC_UNKNOWN;
        x86_intrinsic_fill(bytes, width, result);
        if (!trace->failed && to->place == X86_INTRINSIC_PLACE_MEMORY)
        {
            const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, width);
            if (!trace->failed)
            {
                x86_intrinsic_store(trace, &memory, bytes, width);
            }
        }
        else if (!trace->failed)
        {
            x86_intrinsic_write(trace, to, bytes, width, BUSTER_X86_REST_ZERO, false);
        }
        trace->flags = result;
        trace->flags_boolean = 0;
    }
    else if (operation == X86_OPERATION_XCHG && operands[BUSTER_X86_ROLE_REG].place == X86_INTRINSIC_PLACE_GPR &&
             operands[BUSTER_X86_ROLE_RM].place == X86_INTRINSIC_PLACE_GPR)
    {
        // A swap of two general registers (Tier 0's parallel moves').
        const uint32_t width = operands[BUSTER_X86_ROLE_REG].bytes;
        memcpy(bytes, x86_intrinsic_register_bytes(trace, &operands[BUSTER_X86_ROLE_REG]), width * sizeof(*bytes));
        memcpy(other, x86_intrinsic_register_bytes(trace, &operands[BUSTER_X86_ROLE_RM]), width * sizeof(*other));
        x86_intrinsic_write(trace, &operands[BUSTER_X86_ROLE_REG], other, width, BUSTER_X86_REST_ZERO, false);
        x86_intrinsic_write(trace, &operands[BUSTER_X86_ROLE_RM], bytes, width, BUSTER_X86_REST_ZERO, false);
    }
    else if (operation == X86_OPERATION_SETCC && written >= 0 && x86_intrinsic_condition(form->mnemonic + 3) >= 0)
    {
        // A condition's boolean byte, set<cc>(flags).
        x86_intrinsic_fill(bytes, 1, x86_intrinsic_set(trace, x86_intrinsic_condition(form->mnemonic + 3)));
        if (operands[written].place == X86_INTRINSIC_PLACE_MEMORY)
        {
            const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, 1);
            if (!trace->failed)
            {
                x86_intrinsic_store(trace, &memory, bytes, 1);
            }
        }
        else
        {
            x86_intrinsic_write(trace, &operands[written], bytes, 1, BUSTER_X86_REST_MERGE, false);
        }
    }
    else if (operation == X86_OPERATION_CMOVCC && written >= 0 && operands[written].place == X86_INTRINSIC_PLACE_GPR &&
             x86_intrinsic_condition(form->mnemonic + 4) >= 0 && x86_intrinsic_cmov_boolean(trace, decoded, form, place, operands, (uint32_t)written))
    {
        // Done: a conditional move of 1 or 0 over 0 or a boolean, as the boolean it gives (x86_intrinsic_cmov_boolean).
    }
    else if (x86_intrinsic_compare(form, instruction->immediate, &predicate, &letter, &unsigned_compare) && written >= 0 &&
             operands[written].place == X86_INTRINSIC_PLACE_OPMASK && operands[BUSTER_X86_ROLE_VVVV].read && operands[BUSTER_X86_ROLE_RM].read)
    {
        // An integer compare into an opmask, VPCMPEQ's and VPCMPGT's as VPCMP's predicates (equal, greater), and less
        // and less or equal as greater and greater or equal of the operands the other way round, and those of equal
        // and not equal in their texts' order: vpcmp[u]<b|w|d|q>/<bytes>(left,right)$predicate, under its mask
        // select<element>(mask, node, 0).
        uint32_t left = x86_intrinsic_read(trace, decoded, form, place, &operands[BUSTER_X86_ROLE_VVVV], operands[BUSTER_X86_ROLE_VVVV].bytes, bytes);
        const X86IntrinsicOperand* rm = &operands[BUSTER_X86_ROLE_RM];
        uint32_t right = x86_intrinsic_read(trace, decoded, form, place, rm, rm->place == X86_INTRINSIC_PLACE_MEMORY ? x86_intrinsic_memory_bytes(form) : rm->bytes, other);
        const bool swap = predicate == 1 || predicate == 2 ||
                          ((predicate == 0 || predicate == 3 || predicate == 4 || predicate == 7) && !trace->failed &&
                           x86_intrinsic_node_compare(trace, left, right) > 0);
        predicate = predicate == 1 ? 6 : predicate == 2 ? 5 : predicate;
        unsigned_compare = unsigned_compare && predicate != 0 && predicate != 3 && predicate != 4 && predicate != 7;
        const uint32_t first = swap ? right : left;
        right = swap ? left : right;
        left = first;
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_append(&piece, unsigned_compare ? "vpcmpu" : "vpcmp", unsigned_compare ? 6 : 5);
        x86_intrinsic_append(&piece, &letter, 1);
        x86_intrinsic_appendf(&piece, "/%llu(", 16u << form->vector_length);
        x86_intrinsic_append_node(&piece, trace, left);
        x86_intrinsic_append(&piece, ",", 1);
        x86_intrinsic_append_node(&piece, trace, right);
        x86_intrinsic_appendf(&piece, ")$%llu", predicate);
        uint32_t result = trace->failed ? BUSTER_X86_INTRINSIC_UNKNOWN : x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 8, &piece);
        if (masked && !trace->failed)
        {
            result = x86_intrinsic_select(trace, form, mask, x86_intrinsic_lanes(form, 16u << form->vector_length), result, true, BUSTER_X86_INTRINSIC_UNKNOWN, 8);
        }
        x86_intrinsic_fill(bytes, 8, result);
        if (!trace->failed)
        {
            x86_intrinsic_write(trace, &operands[written], bytes, 8, BUSTER_X86_REST_ZERO, false);
        }
    }
    else
    {
        // An instruction of its own: a node of its form and the values it reads, its immediate and its rounding, and
        // where it is masked, select(mask, node, else): else the destination's old value where it merges, and 0 where
        // it zeroes (an opmask destination's is 0).
        static X86IntrinsicPiece piece;
        piece.length = 0;
        piece.full = false;
        x86_intrinsic_append(&piece, form->mnemonic, (uint32_t)strlen(form->mnemonic));
        x86_intrinsic_appendf(&piece, "#%llu(", x86_intrinsic_canonical[number]);
        uint32_t count = 0;
        for (uint32_t role = 0; role < BUSTER_X86_INTRINSIC_ROLES; role += 1)
        {
            const X86IntrinsicOperand* operand = &operands[role];
            if (operand->read && !trace->failed)
            {
                const bool scalar = role == BUSTER_X86_ROLE_RM && form->operand_bytes != 0 && form->operand_bytes < operand->bytes;
                const uint32_t size = operand->place == X86_INTRINSIC_PLACE_MEMORY ? x86_intrinsic_memory_size(form, effects) : scalar ? form->operand_bytes : operand->bytes;
                const uint32_t node = x86_intrinsic_read(trace, decoded, form, place, operand, size, bytes);
                x86_intrinsic_append(&piece, count != 0 ? "," : "", count != 0 ? 1 : 0);
                x86_intrinsic_append_node(&piece, trace, node);
                count += 1;
            }
        }
        for (uint32_t r = 0; r < 16; r += 1)
        {
            if ((effects->implicit_reads >> r & 1) != 0)
            {
                x86_intrinsic_append(&piece, count != 0 ? "," : "", count != 0 ? 1 : 0);
                x86_intrinsic_append_node(&piece, trace, x86_intrinsic_value(trace, trace->gpr[r], x86_intrinsic_implicit_bytes(number)));
                count += 1;
            }
        }
        // The XMM registers that Key Locker's forms read and write and no role names (x86_implicit_vectors).
        uint32_t vector_reads = 0;
        uint32_t vector_writes = 0;
        if ((effects->properties & BUSTER_X86_EFFECT_IMPLICIT_VECTORS) != 0)
        {
            x86_implicit_vectors(number, &vector_reads, &vector_writes);
        }
        for (uint32_t v = 0; v < 8; v += 1)
        {
            if ((vector_reads >> v & 1) != 0)
            {
                x86_intrinsic_append(&piece, count != 0 ? "," : "", count != 0 ? 1 : 0);
                x86_intrinsic_append_node(&piece, trace, x86_intrinsic_value(trace, trace->vector[v], 16));
                count += 1;
            }
        }
        if (effects->flags_read != 0)
        {
            x86_intrinsic_append(&piece, count != 0 ? ",flags:" : "flags:", count != 0 ? 7 : 6);
            x86_intrinsic_append_node(&piece, trace, trace->flags);
        }
        x86_intrinsic_append(&piece, ")", 1);
        const bool is4 = operands[BUSTER_X86_INTRINSIC_ROLE_IS4].place != X86_INTRINSIC_PLACE_NONE;
        if (form->immediate_bytes != 0 && (!is4 || form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4_IMM || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM))
        {
            x86_intrinsic_appendf(&piece, "$%llu", (uint64_t)(is4 ? (uint32_t)instruction->immediate & 15 : (uint32_t)instruction->immediate & 0xff));
        }
        if ((form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0)
        {
            x86_intrinsic_appendf(&piece, "{r%llu}", (uint64_t)((instruction->decorations & BUSTER_X86_DECORATION_ROUNDING) >> 5));
        }
        const uint32_t written_bytes = written >= 0 ? (operands[written].place == X86_INTRINSIC_PLACE_MEMORY ? x86_intrinsic_memory_size(form, effects)
                                                       : effects->write_bytes != 0 && effects->write_bytes < operands[written].bytes ? effects->write_bytes
                                                                                                                                       : operands[written].bytes)
                                                    : 0;
        uint32_t result = trace->failed ? BUSTER_X86_INTRINSIC_UNKNOWN : x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, written_bytes, &piece);
        if (masked && written >= 0 && !trace->failed)
        {
            // An opmask destination's lanes are its sources' elements, and its masked-off bits zero; a vector's are its
            // own elements, merged or zeroed.
            const bool opmask = operands[written].place == X86_INTRINSIC_PLACE_OPMASK;
            const uint32_t lanes = x86_intrinsic_lanes(form, opmask ? 16u << form->vector_length : written_bytes);
            const uint32_t old = zeroing || opmask ? BUSTER_X86_INTRINSIC_UNKNOWN : x86_intrinsic_read(trace, decoded, form, place, &operands[written], written_bytes, other);
            result = trace->failed ? result : x86_intrinsic_select(trace, form, mask, lanes, result, zeroing || opmask, old, written_bytes);
        }
        x86_intrinsic_fill(bytes, written_bytes, result);
        if (written >= 0 && !trace->failed && operands[written].place == X86_INTRINSIC_PLACE_MEMORY)
        {
            const X86IntrinsicMemory memory = x86_intrinsic_address(trace, decoded, form, place, written_bytes);
            if (!trace->failed)
            {
                x86_intrinsic_store(trace, &memory, bytes, written_bytes);
            }
        }
        else if (written >= 0 && !trace->failed)
        {
            x86_intrinsic_write(trace, &operands[written], bytes, written_bytes, x86_effects_role(effects, (uint32_t)written) >> BUSTER_X86_ROLE_REST_SHIFT & 3,
                                (effects->properties & BUSTER_X86_EFFECT_KEEP_UPPER) != 0);
        }
        for (uint32_t r = 0; r < 16 && !trace->failed; r += 1)
        {
            if ((effects->implicit_writes >> r & 1) != 0)
            {
                x86_intrinsic_fill(trace->gpr[r], 8, result);
            }
        }
        for (uint32_t v = 0; v < 8 && !trace->failed; v += 1)
        {
            if ((vector_writes >> v & 1) != 0)
            {
                // Its 16 bytes are the node's part for the register, node.x<v>; a legacy form keeps the rest.
                static X86IntrinsicPiece part;
                part.length = 0;
                part.full = false;
                x86_intrinsic_append_node(&part, trace, result);
                x86_intrinsic_appendf(&part, ".x%llu", v);
                x86_intrinsic_fill(trace->vector[v], 16, x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 16, &part));
            }
        }
        if (effects->flags_written != 0 || effects->flags_undefined != 0)
        {
            trace->flags = result;
            trace->flags_boolean = x86_intrinsic_tested(trace, decoded, form, effects, operands);
        }
        // An instruction that writes no register or memory, and no flag or is a system instruction (on state the trace
        // doesn't follow): an effect (a fence's, a system instruction's).
        if (written < 0 && effects->implicit_writes == 0 && vector_writes == 0 &&
            (operation == X86_OPERATION_SYSTEM || (effects->flags_written == 0 && effects->flags_undefined == 0)))
        {
            x86_intrinsic_effect(trace, result);
        }
    }
    return action;
}

// ---------------------------------------------------------------------------------------------------------------------
// The walk

// A function of an object, decoded: its section, its place there and its records.
typedef struct X86IntrinsicFunction X86IntrinsicFunction;
struct X86IntrinsicFunction
{
    uint32_t section;
    uint64_t offset;
    X86Decoded* records;
    uint64_t count;
};

static bool x86_intrinsic_decode(const X86IntrinsicObject* object, const Elf64_Sym* symbol, X86IntrinsicFunction* function, char* failure, size_t failure_size)
{
    const Elf64_Shdr* section = &object->sections[symbol->st_shndx];
    const bool inside = symbol->st_value <= section->sh_size && symbol->st_size <= section->sh_size - symbol->st_value && symbol->st_size != 0;
    *function = (X86IntrinsicFunction){ .section = symbol->st_shndx, .offset = symbol->st_value };
    function->records = inside ? malloc(symbol->st_size * sizeof(*function->records)) : 0;
    bool valid = function->records != 0;
    if (valid)
    {
        const uint8_t* code = object->bytes + section->sh_offset + symbol->st_value;
        const X86DecodeResult decoded = x86_disassemble_scalar(code, symbol->st_size, function->records, symbol->st_size);
        function->count = decoded.count;
        valid = decoded.status == X86_DECODE_OK && decoded.consumed == symbol->st_size;
        if (!valid)
        {
            char hex[64] = { 0 };
            for (uint64_t i = 0; i < 12 && decoded.consumed + i < symbol->st_size; i += 1)
            {
                snprintf(hex + 3 * i, sizeof(hex) - 3 * i, "%02x ", code[decoded.consumed + i]);
            }
            snprintf(failure, failure_size, "%s: undecodable at +%llu (status %u): %s", x86_intrinsic_symbol_name(object, symbol),
                     (unsigned long long)decoded.consumed, decoded.status, hex);
        }
    }
    else
    {
        snprintf(failure, failure_size, "%s: no code", x86_intrinsic_symbol_name(object, symbol));
    }
    if (!valid)
    {
        free(function->records);
        function->records = 0;
    }
    return valid;
}

// A path the walk has yet to follow: the trace's state where a branch left it (its stack's and stores' copies), its
// conditions, and the walk's calls and the record it goes on at.
typedef struct X86IntrinsicPending X86IntrinsicPending;
struct X86IntrinsicPending
{
    X86IntrinsicByte gpr[16][8];
    X86IntrinsicByte vector[32][64];
    X86IntrinsicByte opmask[8][8];
    uint32_t flags;
    uint32_t flags_boolean;
    X86IntrinsicByte* stack;
    char* stores;
    uint32_t stores_length;
    uint32_t conditions[BUSTER_X86_INTRINSIC_CONDITIONS];
    bool holds[BUSTER_X86_INTRINSIC_CONDITIONS];
    uint32_t condition_count;
    uint32_t functions[BUSTER_X86_INTRINSIC_CALLS];
    uint64_t returns[BUSTER_X86_INTRINSIC_CALLS];
    uint32_t depth;
    uint64_t index;
};

// A path the walk followed to the function's return: its conditions' text and its result's and stores' text.
typedef struct X86IntrinsicPath X86IntrinsicPath;
struct X86IntrinsicPath
{
    char* conditions;
    char* text;
};

static int x86_intrinsic_path_order(const void* left, const void* right)
{
    return strcmp(((const X86IntrinsicPath*)left)->conditions, ((const X86IntrinsicPath*)right)->conditions);
}

// The index of the decoded function at `section` and `offset` among the walk's, decoding it first; or `capacity` where
// it isn't a function's entry or can't be decoded (with the failure in the trace).
static uint32_t x86_intrinsic_function(X86IntrinsicTrace* trace, X86IntrinsicFunction* functions, uint32_t* count, uint32_t capacity, uint32_t section,
                                       uint64_t offset)
{
    uint32_t found = capacity;
    for (uint32_t f = 0; f < *count; f += 1)
    {
        found = functions[f].section == section && functions[f].offset == offset ? f : found;
    }
    const Elf64_Sym* symbol = found == capacity ? x86_intrinsic_function_at(trace->object, section, offset) : 0;
    if (found == capacity && (symbol == 0 || *count == capacity))
    {
        x86_intrinsic_fail(trace, symbol == 0 ? "a call of no function's entry%s" : "calls of more functions than the trace decodes%s", "");
    }
    else if (found == capacity && !x86_intrinsic_decode(trace->object, symbol, &functions[*count], trace->failure, sizeof(trace->failure)))
    {
        trace->failed = true;
    }
    else if (found == capacity)
    {
        found = *count;
        *count += 1;
    }
    return found;
}

// The text of the path's conditions, each its node's text and =1 where it holds or =0, in their texts' order, joined
// by " & "; with `invert`, each the other way (test-x86-intrinsics' cases whose clang code branches the other way).
static void x86_intrinsic_conditions(const X86IntrinsicTrace* trace, bool invert, char* text, size_t size)
{
    uint32_t order[BUSTER_X86_INTRINSIC_CONDITIONS];
    for (uint32_t c = 0; c < trace->condition_count; c += 1)
    {
        order[c] = c;
    }
    // Insertion sort of at most BUSTER_X86_INTRINSIC_CONDITIONS conditions by their nodes' texts.
    for (uint32_t c = 1; c < trace->condition_count; c += 1)
    {
        const uint32_t moved = order[c];
        uint32_t place = c;
        while (place > 0 && x86_intrinsic_node_compare(trace, trace->conditions[order[place - 1]], trace->conditions[moved]) > 0)
        {
            order[place] = order[place - 1];
            place -= 1;
        }
        order[place] = moved;
    }
    size_t length = 0;
    text[0] = 0;
    for (uint32_t c = 0; c < trace->condition_count && length < size; c += 1)
    {
        const X86IntrinsicNode* node = &trace->nodes[trace->conditions[order[c]]];
        const int written = snprintf(text + length, size - length, "%s%.*s=%u", c != 0 ? " & " : "", (int)node->length, trace->text + node->text,
                                     trace->holds[order[c]] != invert ? 1u : 0u);
        length += written > 0 ? (size_t)written : 0;
    }
}

// Saves the trace's state, the walk's calls and the record it goes on at as a pending path; false where its memory
// can't be had.
static bool x86_intrinsic_save(const X86IntrinsicTrace* trace, X86IntrinsicPending* pending, const uint32_t* functions, const uint64_t* returns, uint32_t depth,
                               uint64_t index)
{
    memcpy(pending->gpr, trace->gpr, sizeof(trace->gpr));
    memcpy(pending->vector, trace->vector, sizeof(trace->vector));
    memcpy(pending->opmask, trace->opmask, sizeof(trace->opmask));
    pending->flags = trace->flags;
    pending->flags_boolean = trace->flags_boolean;
    pending->stack = malloc(BUSTER_X86_INTRINSIC_STACK_SIZE * sizeof(X86IntrinsicByte));
    pending->stores = malloc(trace->stores_length + 1u);
    const bool saved = pending->stack != 0 && pending->stores != 0;
    if (saved)
    {
        memcpy(pending->stack, trace->stack, BUSTER_X86_INTRINSIC_STACK_SIZE * sizeof(X86IntrinsicByte));
        memcpy(pending->stores, trace->stores, trace->stores_length);
        pending->stores_length = trace->stores_length;
        memcpy(pending->conditions, trace->conditions, sizeof(trace->conditions));
        memcpy(pending->holds, trace->holds, sizeof(trace->holds));
        pending->condition_count = trace->condition_count;
        memcpy(pending->functions, functions, sizeof(pending->functions));
        memcpy(pending->returns, returns, sizeof(pending->returns));
        pending->depth = depth;
        pending->index = index;
    }
    else
    {
        free(pending->stack);
        free(pending->stores);
        pending->stack = 0;
        pending->stores = 0;
    }
    return saved;
}

// The pending path's state into the trace and the walk's calls, its copies freed.
static void x86_intrinsic_restore(X86IntrinsicTrace* trace, X86IntrinsicPending* pending, uint32_t* functions, uint64_t* returns, uint32_t* depth, uint64_t* index)
{
    memcpy(trace->gpr, pending->gpr, sizeof(trace->gpr));
    memcpy(trace->vector, pending->vector, sizeof(trace->vector));
    memcpy(trace->opmask, pending->opmask, sizeof(trace->opmask));
    trace->flags = pending->flags;
    trace->flags_boolean = pending->flags_boolean;
    memcpy(trace->stack, pending->stack, BUSTER_X86_INTRINSIC_STACK_SIZE * sizeof(X86IntrinsicByte));
    memcpy(trace->stores, pending->stores, pending->stores_length);
    trace->stores_length = pending->stores_length;
    memcpy(trace->conditions, pending->conditions, sizeof(trace->conditions));
    memcpy(trace->holds, pending->holds, sizeof(trace->holds));
    trace->condition_count = pending->condition_count;
    memcpy(functions, pending->functions, sizeof(pending->functions));
    memcpy(returns, pending->returns, sizeof(pending->returns));
    *depth = pending->depth;
    *index = pending->index;
    free(pending->stack);
    free(pending->stores);
    pending->stack = 0;
    pending->stores = 0;
}

// The trace of the function `name` of the object from its entry, with the arguments' registers and the stack's slots
// as the nodes x86_intrinsic_trace names them, its result's text (the return register's `result` bytes, then each
// store) written into `output`; or why it couldn't follow it. At a conditional branch or xbegin it follows both paths,
// up to BUSTER_X86_INTRINSIC_PATHS: then each path's text follows a line of its conditions (path ...), the paths in
// those lines' order, as x86_intrinsic_conditions writes them (with `invert`, each condition the other way). With
// `listing`, each record it followed, in Intel syntax, with the node its result is.
static bool x86_intrinsic_trace(X86IntrinsicTrace* trace, const char* name, uint8_t result, bool invert, char* output, size_t output_size, FILE* listing)
{
    const X86IntrinsicObject* object = trace->object;
    trace->node_count = 0;
    trace->text_length = 0;
    trace->stores_length = 0;
    trace->condition_count = 0;
    trace->flags_boolean = 0;
    trace->failed = false;
    trace->failure[0] = 0;
    x86_intrinsic_named(trace, X86_INTRINSIC_UNKNOWN, 0, "?");
    x86_intrinsic_named(trace, X86_INTRINSIC_ZERO, 1, "0");
    const uint32_t unknown = BUSTER_X86_INTRINSIC_UNKNOWN;
    for (uint32_t r = 0; r < 16; r += 1)
    {
        for (uint32_t i = 0; i < 8; i += 1)
        {
            trace->gpr[r][i] = (X86IntrinsicByte){ .node = unknown, .index = i };
        }
    }
    static const uint8_t arguments[6] = { X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_RDX, X86_REGISTER_RCX, X86_REGISTER_R8, X86_REGISTER_R9 };
    for (uint32_t a = 0; a < 6; a += 1)
    {
        char argument[8];
        snprintf(argument, sizeof(argument), "g%u", a);
        x86_intrinsic_fill(trace->gpr[arguments[a]], 8, x86_intrinsic_named(trace, X86_INTRINSIC_ARGUMENT, 8, argument));
    }
    for (uint32_t r = 0; r < 32; r += 1)
    {
        char argument[8];
        snprintf(argument, sizeof(argument), "v%u", r);
        const uint32_t node = r < 8 ? x86_intrinsic_named(trace, X86_INTRINSIC_ARGUMENT, 64, argument) : unknown;
        x86_intrinsic_fill(trace->vector[r], 64, node);
    }
    for (uint32_t k = 0; k < 8; k += 1)
    {
        x86_intrinsic_fill(trace->opmask[k], 8, unknown);
    }
    trace->flags = unknown;
    for (uint64_t i = 0; i < BUSTER_X86_INTRINSIC_STACK_SIZE; i += 1)
    {
        trace->stack[i] = (X86IntrinsicByte){ .node = unknown };
    }
    x86_intrinsic_fill(&trace->stack[BUSTER_X86_INTRINSIC_ENTRY - BUSTER_X86_INTRINSIC_STACK_BASE], 8, x86_intrinsic_named(trace, X86_INTRINSIC_VALUE, 8, "ra"));
    for (uint32_t s = 0; 16 + 8 * s <= BUSTER_X86_INTRINSIC_ABOVE; s += 1)
    {
        char argument[8];
        snprintf(argument, sizeof(argument), "s%u", s);
        x86_intrinsic_fill(&trace->stack[BUSTER_X86_INTRINSIC_ENTRY + 8 + 8 * s - BUSTER_X86_INTRINSIC_STACK_BASE], 8,
                           x86_intrinsic_named(trace, X86_INTRINSIC_ARGUMENT, 8, argument));
    }
    x86_intrinsic_set_rsp(trace, BUSTER_X86_INTRINSIC_ENTRY);
    const Elf64_Sym* entry = x86_intrinsic_function_named(object, name);
    // The functions the walk decoded, kept until it ends (a pending path may return into one), and its calls: each
    // depth's function and the record its return goes on at.
    X86IntrinsicFunction decoded[BUSTER_X86_INTRINSIC_FUNCTIONS] = { 0 };
    uint32_t decoded_count = 0;
    uint32_t functions[BUSTER_X86_INTRINSIC_CALLS] = { 0 };
    uint64_t returns[BUSTER_X86_INTRINSIC_CALLS] = { 0 };
    static X86IntrinsicPending pending[BUSTER_X86_INTRINSIC_PATHS];
    uint32_t pending_count = 0;
    X86IntrinsicPath paths[BUSTER_X86_INTRINSIC_PATHS] = { 0 };
    uint32_t path_count = 0;
    uint32_t depth = 0;
    uint64_t index = 0;
    bool walking = false;
    if (entry == 0)
    {
        x86_intrinsic_fail(trace, "%s isn't a function of the object", name);
    }
    else
    {
        functions[0] = x86_intrinsic_function(trace, decoded, &decoded_count, BUSTER_X86_INTRINSIC_FUNCTIONS, entry->st_shndx, entry->st_value);
        walking = !trace->failed;
    }
    for (uint32_t steps = 0; walking && !trace->failed; steps += 1)
    {
        const X86IntrinsicFunction* function = &decoded[functions[depth]];
        if (steps == BUSTER_X86_INTRINSIC_STEPS || index >= function->count)
        {
            x86_intrinsic_fail(trace, steps == BUSTER_X86_INTRINSIC_STEPS ? "%s: more steps than the trace takes" : "%s: past the function's end", name);
        }
        else
        {
            const X86Decoded* record = &function->records[index];
            const X86IntrinsicPlaceInCode place = { .section = function->section, .offset = function->offset + record->offset, .length = record->length };
            uint64_t target = 0;
            const uint32_t before = trace->node_count;
            const X86IntrinsicAction action = x86_intrinsic_step(trace, record, place, &target);
            if (listing != 0)
            {
                char text[256];
                if (x86_disassembly_format(record, text, sizeof(text)) < 0)
                {
                    snprintf(text, sizeof(text), "?");
                }
                fprintf(listing, "  %*s%-48s", 2 * depth, "", text);
                if (trace->node_count > before && !trace->failed)
                {
                    const X86IntrinsicNode* node = &trace->nodes[trace->node_count - 1];
                    fprintf(listing, " %.*s", (int)(node->length < 160 ? node->length : 160), trace->text + node->text);
                }
                fprintf(listing, "\n");
            }
            // A jump's or a branch's target: the record at that offset of the function.
            uint64_t found = function->count;
            for (uint64_t i = 0; (action == X86_INTRINSIC_JUMP || action == X86_INTRINSIC_BRANCH) && i < function->count; i += 1)
            {
                found = function->offset + function->records[i].offset == target ? i : found;
            }
            if (trace->failed)
            {
            }
            else if (action == X86_INTRINSIC_CALL && depth + 1 == BUSTER_X86_INTRINSIC_CALLS)
            {
                x86_intrinsic_fail(trace, "%s: calls deeper than the trace follows", name);
            }
            else if (action == X86_INTRINSIC_CALL)
            {
                const uint32_t callee = x86_intrinsic_function(trace, decoded, &decoded_count, BUSTER_X86_INTRINSIC_FUNCTIONS, function->section, target);
                if (!trace->failed)
                {
                    returns[depth] = index + 1;
                    depth += 1;
                    functions[depth] = callee;
                    index = 0;
                }
            }
            else if ((action == X86_INTRINSIC_JUMP || action == X86_INTRINSIC_BRANCH) && found == function->count)
            {
                x86_intrinsic_fail(trace, "%s: a jump to no record of its function", name);
            }
            else if (action == X86_INTRINSIC_JUMP)
            {
                index = found;
            }
            else if (action == X86_INTRINSIC_BRANCH && (pending_count + path_count + 1 >= BUSTER_X86_INTRINSIC_PATHS ||
                                                        trace->condition_count == BUSTER_X86_INTRINSIC_CONDITIONS))
            {
                x86_intrinsic_fail(trace, "%s: more paths or conditions than the trace follows", name);
            }
            else if (action == X86_INTRINSIC_BRANCH)
            {
                // The taken path waits, its condition holding as the branch says (and xbegin's abort status in EAX);
                // the walk goes on with the other.
                trace->conditions[trace->condition_count] = trace->branch_condition;
                trace->holds[trace->condition_count] = trace->branch_holds;
                trace->condition_count += 1;
                X86IntrinsicPending* taken = &pending[pending_count];
                if (!x86_intrinsic_save(trace, taken, functions, returns, depth, found))
                {
                    x86_intrinsic_fail(trace, "%s: no memory for a branch's path", name);
                }
                else
                {
                    pending_count += 1;
                    if (trace->branch_abort)
                    {
                        static X86IntrinsicPiece piece;
                        piece.length = 0;
                        piece.full = false;
                        x86_intrinsic_append(&piece, "status(", 7);
                        x86_intrinsic_append_node(&piece, trace, trace->branch_condition);
                        x86_intrinsic_append(&piece, ")", 1);
                        x86_intrinsic_fill(taken->gpr[X86_REGISTER_RAX], 4, x86_intrinsic_node(trace, X86_INTRINSIC_VALUE, 4, &piece));
                        x86_intrinsic_zero(&taken->gpr[X86_REGISTER_RAX][4], 4);
                    }
                    trace->holds[trace->condition_count - 1] = !trace->branch_holds;
                    index += 1;
                }
            }
            else if (action == X86_INTRINSIC_RETURN && depth == 0)
            {
                // The path's end: its conditions and its result's and stores' text, then the next pending path.
                static const uint8_t sizes[] = { 16, 32, 64, 1, 2, 4, 8, 0 };
                const uint32_t size = sizes[result];
                const uint32_t node = size == 0 ? BUSTER_X86_INTRINSIC_UNKNOWN
                                      : result <= X86_INTRINSIC_RETURN_VECTOR64 ? x86_intrinsic_value(trace, trace->vector[0], size)
                                                                                : x86_intrinsic_value(trace, trace->gpr[X86_REGISTER_RAX], size);
                const X86IntrinsicNode* value = &trace->nodes[node];
                X86IntrinsicPath* path = &paths[path_count];
                path->conditions = malloc(4096);
                path->text = malloc(output_size);
                if (path->conditions == 0 || path->text == 0)
                {
                    free(path->conditions);
                    free(path->text);
                    *path = (X86IntrinsicPath){ 0 };
                    x86_intrinsic_fail(trace, "%s: no memory for a path's text", name);
                }
                else
                {
                    x86_intrinsic_conditions(trace, invert, path->conditions, 4096);
                    snprintf(path->text, output_size, "return %.*s\n%.*s", size == 0 ? 4 : (int)value->length, size == 0 ? "none" : trace->text + value->text,
                             (int)trace->stores_length, trace->stores);
                    path_count += 1;
                }
                walking = pending_count != 0;
                if (walking && !trace->failed)
                {
                    pending_count -= 1;
                    x86_intrinsic_restore(trace, &pending[pending_count], functions, returns, &depth, &index);
                    if (listing != 0)
                    {
                        fprintf(listing, "  (the other path of a branch)\n");
                    }
                }
            }
            else if (action == X86_INTRINSIC_RETURN)
            {
                depth -= 1;
                index = returns[depth];
            }
            else
            {
                index += 1;
            }
        }
    }
    const bool done = !walking && !trace->failed && path_count != 0;
    if (done)
    {
        // One path without conditions is the text alone; paths are each their conditions' line and their text, in the
        // lines' order.
        qsort(paths, path_count, sizeof(*paths), x86_intrinsic_path_order);
        size_t length = 0;
        output[0] = 0;
        for (uint32_t p = 0; p < path_count && length < output_size; p += 1)
        {
            const bool alone = path_count == 1 && paths[p].conditions[0] == 0;
            const int written = alone ? snprintf(output + length, output_size - length, "%s", paths[p].text)
                                      : snprintf(output + length, output_size - length, "path %s\n%s", paths[p].conditions, paths[p].text);
            length += written > 0 ? (size_t)written : 0;
        }
    }
    for (uint32_t p = 0; p < pending_count; p += 1)
    {
        free(pending[p].stack);
        free(pending[p].stores);
        pending[p].stack = 0;
        pending[p].stores = 0;
    }
    for (uint32_t p = 0; p < path_count; p += 1)
    {
        free(paths[p].conditions);
        free(paths[p].text);
    }
    for (uint32_t f = 0; f < decoded_count; f += 1)
    {
        free(decoded[f].records);
    }
    return done;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool x86_intrinsic_run(char* const* arguments, const char* log)
{
    const pid_t child = fork();
    bool success = child >= 0;
    if (child == 0)
    {
        const int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0 && dup2(fd, STDOUT_FILENO) >= 0 && dup2(fd, STDERR_FILENO) >= 0)
        {
            close(fd);
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        success = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return success;
}

static bool x86_intrinsic_same_files(const char* first, const char* second)
{
    X86IntrinsicObject a;
    X86IntrinsicObject b;
    const bool read = x86_intrinsic_object_read(&a, first) && x86_intrinsic_object_read(&b, second);
    const bool same = read && a.length == b.length && memcmp(a.bytes, b.bytes, a.length) == 0;
    if (read)
    {
        free(a.bytes);
        free(b.bytes);
    }
    return same;
}

static void x86_intrinsic_print_log(const char* path)
{
    FILE* file = fopen(path, "r");
    char line[512];
    for (uint32_t lines = 0; file != 0 && lines < 40 && fgets(line, sizeof(line), file) != 0; lines += 1)
    {
        fprintf(stderr, "    %s", line);
    }
    if (file != 0)
    {
        fclose(file);
    }
}

// Equal separately appended slices must stop at the guarded text tail, including in the actual opmask step. Once
// both slices fill the arena, only the following result reservation may fail; with room, it must keep their expression.
static bool x86_intrinsic_test_node_text(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    uint64_t mapped = 0;
    uint8_t* end = page_size > 0 ? x86_map_guarded(BUSTER_X86_INTRINSIC_TEXT, (uint64_t)page_size, &mapped) : 0;
    bool valid = end != 0;
    if (valid)
    {
        X86IntrinsicNode nodes[8] = { 0 };
        X86IntrinsicTrace trace = { .nodes = nodes, .text = (char*)end - BUSTER_X86_INTRINSIC_TEXT };
        memcpy(end - 10, "v0:16v0:16", 10);
        nodes[0] = (X86IntrinsicNode){ .text = BUSTER_X86_INTRINSIC_TEXT - 10, .length = 5 };
        nodes[1] = (X86IntrinsicNode){ .text = BUSTER_X86_INTRINSIC_TEXT - 5, .length = 5 };
        valid = x86_intrinsic_node_compare(&trace, 0, 1) == 0 && x86_intrinsic_node_compare(&trace, 1, 0) == 0;
        nodes[1].length = 4;
        valid = x86_intrinsic_node_compare(&trace, 0, 1) > 0 && x86_intrinsic_node_compare(&trace, 1, 0) < 0 && valid;
        nodes[1].length = 5;
        end[-1] = '7';
        valid = x86_intrinsic_node_compare(&trace, 0, 1) < 0 && x86_intrinsic_node_compare(&trace, 1, 0) > 0 && valid;

        // vpcmpeqb k1,xmm0,xmm0: each read appends v0:16 from the same 64-byte argument. The final read ends
        // exactly at PROT_NONE. There is no terminator, and the two equal slices must compare within their five bytes.
        const X86Decoded compare = { .instruction = { .form = X86_FORM_VPCMPEQB_K_X_XM128, .reg = 1,
                                                      .mode = BUSTER_X86_REGISTER_DIRECT } };
        for (uint32_t full = 0; full < 2; full += 1)
        {
            memset(nodes, 0, sizeof(nodes));
            trace = (X86IntrinsicTrace){ .nodes = nodes, .node_count = 2, .text = (char*)end - BUSTER_X86_INTRINSIC_TEXT,
                                         .text_length = full != 0 ? BUSTER_X86_INTRINSIC_TEXT - 12 : 0 };
            const uint32_t argument = x86_intrinsic_named(&trace, X86_INTRINSIC_ARGUMENT, 64, "v0");
            x86_intrinsic_fill(trace.vector[0], 64, argument);
            uint64_t target = 0;
            const X86IntrinsicAction action = x86_intrinsic_step(&trace, &compare, (X86IntrinsicPlaceInCode){ 0 }, &target);
            if (full != 0)
            {
                valid = action == X86_INTRINSIC_NEXT && trace.failed && trace.node_count == 5 &&
                        trace.text_length == BUSTER_X86_INTRINSIC_TEXT && memcmp(end - 10, "v0:16v0:16", 10) == 0 &&
                        strcmp(trace.failure, "the trace's values are longer than it keeps") == 0 && valid;
            }
            else
            {
                static const char expected[] = "vpcmpb/16(v0:16,v0:16)$0";
                const uint32_t result = trace.opmask[1][0].node;
                valid = action == X86_INTRINSIC_NEXT && !trace.failed && result > BUSTER_X86_INTRINSIC_ZERO && result < trace.node_count &&
                        nodes[result].length == sizeof(expected) - 1 &&
                        memcmp(trace.text + nodes[result].text, expected, sizeof(expected) - 1) == 0 && valid;
            }
        }
        munmap(end + (uint64_t)page_size - mapped, mapped);
    }
    return valid;
}

static int run_x86_intrinsic_tests(int argc, char** argv)
{
    const char* executable = "/proc/self/exe";
    const char* clang = "clang";
    const char* march = "znver5";
    const char* print = 0;
    const char* listed = 0;
    // The options every compile takes after -march (--option), as -mno-apxf, which keeps diamondrapids' prologues free of
    // APX's prefixes, which the disassembler doesn't decode.
    char* options[4] = { 0 };
    uint32_t option_count = 0;
    bool keep = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--march") == 0 && i + 1 < argc)
        {
            march = argv[++i];
        }
        else if (strcmp(argv[i], "--print") == 0 && i + 1 < argc)
        {
            print = argv[++i];
        }
        else if (strcmp(argv[i], "--cases") == 0 && i + 1 < argc)
        {
            listed = argv[++i];
        }
        else if (strcmp(argv[i], "--option") == 0 && i + 1 < argc && option_count < BUSTER_ARRAY_LENGTH(options))
        {
            options[option_count++] = argv[++i];
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "test-x86-intrinsics [--executable PATH] [--clang PATH] [--march CPU] [--option FLAG]... [--cases FILE] [--print NAME] [--keep]: invalid "
                        "arguments\n");
    }
    // The cases: the table's, or a file's lines, each a result's letter (X, Y and Z for vectors of 16, 32 and 64 bytes,
    // I and L for integers of 4 and 8, V for none) and an intrinsic's name.
    static X86IntrinsicCase cases[8192];
    static char names_text[8192 * 64];
    uint32_t case_count = 0;
    if (listed == 0)
    {
        case_count = BUSTER_ARRAY_LENGTH(x86_intrinsic_cases);
        memcpy(cases, x86_intrinsic_cases, sizeof(x86_intrinsic_cases));
    }
    FILE* list = valid && listed != 0 ? fopen(listed, "r") : 0;
    valid = valid && (listed == 0 || list != 0);
    char line[128];
    uint32_t used = 0;
    while (list != 0 && valid && fgets(line, sizeof(line), list) != 0)
    {
        char letter = 0;
        char name[64];
        if (sscanf(line, " %c %63s", &letter, name) == 2 && case_count < BUSTER_ARRAY_LENGTH(cases) && used + strlen(name) + 1 <= sizeof(names_text))
        {
            // The result kinds' letters in X86IntrinsicReturn's order, as the cases' macros name them.
            const char* letters = "XYZBHILV";
            const char* found = letter != 0 ? strchr(letters, letter) : 0;
            valid = found != 0;
            memcpy(names_text + used, name, strlen(name) + 1);
            cases[case_count++] = (X86IntrinsicCase){ .name = names_text + used, .result = found != 0 ? (uint8_t)(found - letters) : 0 };
            used += (uint32_t)strlen(name) + 1;
        }
    }
    if (list != 0)
    {
        fclose(list);
    }
    const char* temporary = getenv("TMPDIR");
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/buster-intrinsics-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    valid = valid && mkdtemp(directory) != 0 && x86_disassembler_initialize() && x86_semantics_initialize();
    if (valid)
    {
        x86_intrinsic_canonical_forms();
    }
    char source[600];
    char objects[3][600];
    char logs[3][600];
    snprintf(source, sizeof(source), "%s/intrinsics.c", directory);
    static const char* const names[3] = { "clang", "ours", "ours-scalar" };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        snprintf(objects[i], sizeof(objects[i]), "%s/%s.o", directory, names[i]);
        snprintf(logs[i], sizeof(logs[i]), "%s/%s.log", directory, names[i]);
    }
    FILE* file = valid ? fopen(source, "w") : 0;
    valid = valid && file != 0;
    if (valid)
    {
        fprintf(file, "#include <x86intrin.h>\n#include <immintrin.h>\n%s", x86_intrinsic_probe_functions);
        fprintf(file, "void* buster_intrinsics[] = {\n");
        for (uint32_t i = 0; i < case_count; i += 1)
        {
            fprintf(file, "    (void*)%s,\n", cases[i].name);
        }
        fprintf(file, "};\n");
        valid = fclose(file) == 0;
    }
    char march_option[64];
    snprintf(march_option, sizeof(march_option), "-march=%s", march);
    char* compiles[3][16] = {
        { (char*)clang, "-O0", march_option },
        { (char*)executable, march_option },
        { (char*)executable, "--scalar", march_option },
    };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        uint32_t count = i == 1 ? 2 : 3;
        for (uint32_t k = 0; k < option_count; k += 1)
        {
            compiles[i][count++] = options[k];
        }
        compiles[i][count++] = "-c";
        compiles[i][count++] = "-o";
        compiles[i][count++] = objects[i];
        compiles[i][count++] = source;
        compiles[i][count] = 0;
    }
    uint64_t checks = 0;
    uint64_t failures = 0;
    const bool bounded_text = valid && x86_intrinsic_test_node_text();
    checks += valid;
    failures += valid && !bounded_text;
    if (valid && !bounded_text)
    {
        fprintf(stderr, "test-x86-intrinsics: node text ordering or the guarded equal-operand compare is wrong\n");
    }
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        const bool compiled = x86_intrinsic_run(compiles[i], logs[i]);
        if (!compiled)
        {
            fprintf(stderr, "test-x86-intrinsics: %s's compile of %s failed:\n", names[i], source);
            x86_intrinsic_print_log(logs[i]);
        }
        valid = compiled;
    }
    checks += valid;
    const bool same = valid && x86_intrinsic_same_files(objects[1], objects[2]);
    failures += valid && !same;
    if (valid && !same)
    {
        fprintf(stderr, "test-x86-intrinsics: the scalar path's object isn't the vector path's\n");
    }
    X86IntrinsicObject theirs = { 0 };
    X86IntrinsicObject ours = { 0 };
    valid = valid && x86_intrinsic_object_read(&theirs, objects[0]) && x86_intrinsic_object_read(&ours, objects[1]);
    static X86IntrinsicTrace traces[2];
    for (uint32_t t = 0; valid && t < 2; t += 1)
    {
        traces[t].object = t == 0 ? &theirs : &ours;
        traces[t].nodes = traces[t].nodes != 0 ? traces[t].nodes : malloc(BUSTER_X86_INTRINSIC_NODES * sizeof(X86IntrinsicNode));
        traces[t].text = traces[t].text != 0 ? traces[t].text : malloc(BUSTER_X86_INTRINSIC_TEXT);
        traces[t].stores = traces[t].stores != 0 ? traces[t].stores : malloc(BUSTER_X86_INTRINSIC_TEXT);
        traces[t].stack = traces[t].stack != 0 ? traces[t].stack : malloc(BUSTER_X86_INTRINSIC_STACK_SIZE * sizeof(X86IntrinsicByte));
        valid = traces[t].nodes != 0 && traces[t].text != 0 && traces[t].stores != 0 && traces[t].stack != 0;
    }
    static char results[2][BUSTER_X86_INTRINSIC_PIECE];
    uint32_t compared = 0;
    for (uint32_t c = 0; valid && c < case_count; c += 1)
    {
        const X86IntrinsicCase* entry = &cases[c];
        const bool printed = print != 0 && strcmp(print, entry->name) == 0;
        bool traced[2];
        for (uint32_t t = 0; t < 2; t += 1)
        {
            if (printed)
            {
                printf("%s, %s's code:\n", entry->name, t == 0 ? "clang" : "the compiler");
            }
            traced[t] = x86_intrinsic_trace(&traces[t], entry->name, entry->result, entry->inverted && t == 0, results[t], sizeof(results[t]), printed ? stdout : 0);
        }
        const bool agree = traced[0] && traced[1] && strcmp(results[0], results[1]) == 0;
        checks += 1;
        failures += !agree;
        compared += agree;
        if (!agree || printed)
        {
            fprintf(agree ? stdout : stderr, "%s: %s\n  clang:        %s%s\n  the compiler: %s%s\n", entry->name, agree ? "agree" : "differ",
                    traced[0] ? results[0] : traces[0].failure, traced[0] ? "" : "\n", traced[1] ? results[1] : traces[1].failure, traced[1] ? "" : "\n");
        }
    }
    // The controls: the two orders of one intrinsic's operands must differ, by both compilers' code.
    bool swapped = valid;
    for (uint32_t t = 0; valid && t < 2; t += 1)
    {
        swapped = x86_intrinsic_trace(&traces[t], "buster_intrinsic_sub_ab", X86_INTRINSIC_RETURN_VECTOR16, false, results[0], sizeof(results[0]), 0) &&
                  x86_intrinsic_trace(&traces[t], "buster_intrinsic_sub_ba", X86_INTRINSIC_RETURN_VECTOR16, false, results[1], sizeof(results[1]), 0) &&
                  strcmp(results[0], results[1]) != 0 && swapped;
    }
    checks += valid;
    failures += valid && !swapped;
    if (valid && !swapped)
    {
        fprintf(stderr, "test-x86-intrinsics: the operands' two orders of _mm_sub_epi32 give the same expression\n");
    }
    free(theirs.bytes);
    free(ours.bytes);
    for (uint32_t t = 0; t < 2; t += 1)
    {
        free(traces[t].nodes);
        free(traces[t].text);
        free(traces[t].stores);
        free(traces[t].stack);
        traces[t] = (X86IntrinsicTrace){ 0 };
    }
    failures += !valid;
    if (valid && failures == 0 && !keep)
    {
        unlink(source);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            unlink(objects[i]);
            unlink(logs[i]);
        }
        rmdir(directory);
    }
    else if (directory[0] != 0)
    {
        fprintf(stderr, "test-x86-intrinsics: the files are in %s\n", directory);
    }
    for (uint32_t i = 0; i < x86_intrinsic_uncovered_count; i += 1)
    {
        const uint32_t form = x86_intrinsic_uncovered[i];
        const X86FormDescription* description = x86_form_description(form);
        fprintf(stderr, "test-x86-intrinsics: a trace met %s (form %u, %s, %u bytes), whose form x86_semantics.c has no effects for\n", description->mnemonic,
                form, description->family == BUSTER_X86_FAMILY_LEGACY ? "legacy" : description->family == BUSTER_X86_FAMILY_VEX ? "VEX" : "EVEX",
                description->operand_bytes);
    }
    printf("x86 intrinsics: %u of %u cases' code agrees with clang -O0's; %llu checks, %llu failures\n", compared, case_count,
           (unsigned long long)checks, (unsigned long long)failures);
    return failures == 0 ? 0 : 1;
}
