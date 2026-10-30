// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's x86 post-prototype immediate-constraint actions. Original expected rule facts below were independently
// audited from llvmorg-23.1.1 SemaX86.cpp, not inferred from the production table. They exercise all 353 fact rows,
// exact-name joins, both rules of rndscale, producer ICE proofs, context-sensitive diagnostic events and exact
// guarded slices. External Clang sources separately qualify actual builtin prototypes and diagnostic behavior.
// This action-layer qualification establishes no feature availability, source dispatcher or runtime lowering.

#define BUSTER_CHECK_BUILTIN_X86_TEST_BATCH 19u
#define BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS 64u

typedef struct CheckBuiltinX86Test CheckBuiltinX86Test;
struct CheckBuiltinX86Test
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CheckBuiltinX86TestFact CheckBuiltinX86TestFact;
struct CheckBuiltinX86TestFact
{
    const char* name;
    CheckBuiltinX86Constraint constraint;
    uint32_t argument;
    uint32_t low;
    uint32_t high;
    uint32_t accepted;
    uint32_t source_line;
};

// Primary: https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaX86.cpp
static const CheckBuiltinX86TestFact check_builtin_x86_test_facts[] =
{
    { "__builtin_ia32_cmpb128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 640 },
    { "__builtin_ia32_cmpb256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 644 },
    { "__builtin_ia32_cmpb512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 648 },
    { "__builtin_ia32_cmpd128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 642 },
    { "__builtin_ia32_cmpd256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 646 },
    { "__builtin_ia32_cmpd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 650 },
    { "__builtin_ia32_cmpq128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 643 },
    { "__builtin_ia32_cmpq256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 647 },
    { "__builtin_ia32_cmpq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 651 },
    { "__builtin_ia32_cmpw128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 641 },
    { "__builtin_ia32_cmpw256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 645 },
    { "__builtin_ia32_cmpw512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 649 },
    { "__builtin_ia32_cvtpd2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 202 },
    { "__builtin_ia32_cvtpd2ps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 201 },
    { "__builtin_ia32_cvtpd2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 203 },
    { "__builtin_ia32_cvtpd2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 204 },
    { "__builtin_ia32_cvtpd2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 205 },
    { "__builtin_ia32_cvtps2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 206 },
    { "__builtin_ia32_cvtps2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 207 },
    { "__builtin_ia32_cvtps2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 208 },
    { "__builtin_ia32_cvtps2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 209 },
    { "__builtin_ia32_cvtqq2ps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 211 },
    { "__builtin_ia32_cvtsd2ss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 251 },
    { "__builtin_ia32_cvtss2sd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 104 },
    { "__builtin_ia32_cvttpd2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 70 },
    { "__builtin_ia32_cvttpd2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 71 },
    { "__builtin_ia32_cvttpd2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 72 },
    { "__builtin_ia32_cvttpd2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 73 },
    { "__builtin_ia32_cvttps2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 74 },
    { "__builtin_ia32_cvttps2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 75 },
    { "__builtin_ia32_cvttps2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 76 },
    { "__builtin_ia32_cvttps2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 77 },
    { "__builtin_ia32_cvtuqq2ps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 213 },
    { "__builtin_ia32_extracti32x4_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 600 },
    { "__builtin_ia32_getexppd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 84 },
    { "__builtin_ia32_getexpph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 86 },
    { "__builtin_ia32_getexpps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 85 },
    { "__builtin_ia32_getexpsd128_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 105 },
    { "__builtin_ia32_getexpsh128_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 107 },
    { "__builtin_ia32_getexpss128_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 106 },
    { "__builtin_ia32_inserti64x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 585 },
    { "__builtin_ia32_maxpd512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 59 },
    { "__builtin_ia32_maxph512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 63 },
    { "__builtin_ia32_maxps512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 60 },
    { "__builtin_ia32_maxsd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 111 },
    { "__builtin_ia32_maxsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 113 },
    { "__builtin_ia32_maxss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 112 },
    { "__builtin_ia32_minpd512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 61 },
    { "__builtin_ia32_minph512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 64 },
    { "__builtin_ia32_minps512", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 4368, 62 },
    { "__builtin_ia32_minsd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 114 },
    { "__builtin_ia32_minsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 116 },
    { "__builtin_ia32_minss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 115 },
    { "__builtin_ia32_prefetchi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 2, 3, 0, 948 },
    { "__builtin_ia32_prord512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 793 },
    { "__builtin_ia32_pshufd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 753 },
    { "__builtin_ia32_pslldqi128_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 814 },
    { "__builtin_ia32_pternlogd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 915 },
    { "__builtin_ia32_pternlogq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 917 },
    { "__builtin_ia32_rndscalepd_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 770 },
    { "__builtin_ia32_rndscalepd_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 120 },
    { "__builtin_ia32_rndscaleps_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 769 },
    { "__builtin_ia32_rndscaleps_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 121 },
    { "__builtin_ia32_scalefpd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 246 },
    { "__builtin_ia32_scalefph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 245 },
    { "__builtin_ia32_scalefps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 247 },
    { "__builtin_ia32_scalefsd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 248 },
    { "__builtin_ia32_scalefsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 250 },
    { "__builtin_ia32_scalefss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 249 },
    { "__builtin_ia32_shuf_i64x2", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 854 },
    { "__builtin_ia32_sqrtsd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 254 },
    { "__builtin_ia32_sqrtsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 256 },
    { "__builtin_ia32_sqrtss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 255 },
    { "__builtin_ia32_ucmpb512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 660 },
    { "__builtin_ia32_ucmpq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 663 },
    { "__builtin_ia32_vcomish", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 89 },
    { "__builtin_ia32_vcvt2ps2phx512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 306 },
    { "__builtin_ia32_vcvtdq2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 214 },
    { "__builtin_ia32_vcvtpd2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 199 },
    { "__builtin_ia32_vcvtph2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 220 },
    { "__builtin_ia32_vcvtph2pd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 67 },
    { "__builtin_ia32_vcvtph2ps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 90 },
    { "__builtin_ia32_vcvtph2psx512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 68 },
    { "__builtin_ia32_vcvtph2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 222 },
    { "__builtin_ia32_vcvtph2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 221 },
    { "__builtin_ia32_vcvtph2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 223 },
    { "__builtin_ia32_vcvtph2uw512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 219 },
    { "__builtin_ia32_vcvtph2w512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 218 },
    { "__builtin_ia32_vcvtps2phx512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 200 },
    { "__builtin_ia32_vcvtqq2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 224 },
    { "__builtin_ia32_vcvtsd2sh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 253 },
    { "__builtin_ia32_vcvtsd2usi32", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 156 },
    { "__builtin_ia32_vcvtsd2usi64", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 157 },
    { "__builtin_ia32_vcvtsh2sd_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 102 },
    { "__builtin_ia32_vcvtsh2si32", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 162 },
    { "__builtin_ia32_vcvtsh2si64", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 163 },
    { "__builtin_ia32_vcvtsh2ss_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 4368, 103 },
    { "__builtin_ia32_vcvtsh2usi32", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 164 },
    { "__builtin_ia32_vcvtsh2usi64", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 165 },
    { "__builtin_ia32_vcvtss2sh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 252 },
    { "__builtin_ia32_vcvtss2usi32", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 160 },
    { "__builtin_ia32_vcvtss2usi64", CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 3856, 161 },
    { "__builtin_ia32_vcvttpd2dqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 310 },
    { "__builtin_ia32_vcvttpd2qqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 312 },
    { "__builtin_ia32_vcvttpd2udqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 311 },
    { "__builtin_ia32_vcvttpd2uqqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 313 },
    { "__builtin_ia32_vcvttph2dq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 80 },
    { "__builtin_ia32_vcvttph2qq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 82 },
    { "__builtin_ia32_vcvttph2udq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 81 },
    { "__builtin_ia32_vcvttph2uqq512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 83 },
    { "__builtin_ia32_vcvttph2uw512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 79 },
    { "__builtin_ia32_vcvttph2w512_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 78 },
    { "__builtin_ia32_vcvttps2dqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 314 },
    { "__builtin_ia32_vcvttps2qqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 316 },
    { "__builtin_ia32_vcvttps2udqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 315 },
    { "__builtin_ia32_vcvttps2uqqs512_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 4368, 317 },
    { "__builtin_ia32_vcvttsd2si32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 37 },
    { "__builtin_ia32_vcvttsd2si64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 38 },
    { "__builtin_ia32_vcvttsd2usi32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 39 },
    { "__builtin_ia32_vcvttsd2usi64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 40 },
    { "__builtin_ia32_vcvttsh2si32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 45 },
    { "__builtin_ia32_vcvttsh2si64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 46 },
    { "__builtin_ia32_vcvttsh2usi32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 47 },
    { "__builtin_ia32_vcvttsh2usi64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 48 },
    { "__builtin_ia32_vcvttss2si32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 41 },
    { "__builtin_ia32_vcvttss2si64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 42 },
    { "__builtin_ia32_vcvttss2usi32", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 43 },
    { "__builtin_ia32_vcvttss2usi64", CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 4368, 44 },
    { "__builtin_ia32_vcvtudq2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 215 },
    { "__builtin_ia32_vcvtuqq2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 225 },
    { "__builtin_ia32_vcvtuw2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 217 },
    { "__builtin_ia32_vcvtw2ph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 3856, 216 },
    { "__builtin_ia32_vec_ext_v4di", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 598 },
    { "__builtin_ia32_vextractf128_pd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 561 },
    { "__builtin_ia32_vextractf128_ps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 562 },
    { "__builtin_ia32_vextractf128_si256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 563 },
    { "__builtin_ia32_vfcmaddcph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 299 },
    { "__builtin_ia32_vfcmaddcph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 301 },
    { "__builtin_ia32_vfcmaddcph512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 300 },
    { "__builtin_ia32_vfcmaddcsh_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 296 },
    { "__builtin_ia32_vfcmaddcsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 297 },
    { "__builtin_ia32_vfcmaddcsh_round_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 298 },
    { "__builtin_ia32_vfcmulcph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 305 },
    { "__builtin_ia32_vfcmulcsh_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 304 },
    { "__builtin_ia32_vfmaddcph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 293 },
    { "__builtin_ia32_vfmaddcph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 295 },
    { "__builtin_ia32_vfmaddcph512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 294 },
    { "__builtin_ia32_vfmaddcsh_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 290 },
    { "__builtin_ia32_vfmaddcsh_round_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 291 },
    { "__builtin_ia32_vfmaddcsh_round_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 292 },
    { "__builtin_ia32_vfmaddph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 274 },
    { "__builtin_ia32_vfmaddph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 276 },
    { "__builtin_ia32_vfmaddph512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 275 },
    { "__builtin_ia32_vfmaddsd3_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 257 },
    { "__builtin_ia32_vfmaddsd3_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 259 },
    { "__builtin_ia32_vfmaddsd3_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 258 },
    { "__builtin_ia32_vfmaddsh3_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 263 },
    { "__builtin_ia32_vfmaddsh3_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 265 },
    { "__builtin_ia32_vfmaddsh3_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 264 },
    { "__builtin_ia32_vfmaddss3_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 260 },
    { "__builtin_ia32_vfmaddss3_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 262 },
    { "__builtin_ia32_vfmaddss3_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 261 },
    { "__builtin_ia32_vfmaddsubpd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 278 },
    { "__builtin_ia32_vfmaddsubpd512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 280 },
    { "__builtin_ia32_vfmaddsubpd512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 279 },
    { "__builtin_ia32_vfmaddsubph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 286 },
    { "__builtin_ia32_vfmaddsubph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 288 },
    { "__builtin_ia32_vfmaddsubph512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 287 },
    { "__builtin_ia32_vfmaddsubps512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 282 },
    { "__builtin_ia32_vfmaddsubps512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 284 },
    { "__builtin_ia32_vfmaddsubps512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 283 },
    { "__builtin_ia32_vfmsubaddpd512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 281 },
    { "__builtin_ia32_vfmsubaddph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 289 },
    { "__builtin_ia32_vfmsubaddps512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 285 },
    { "__builtin_ia32_vfmsubph512_mask3", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 277 },
    { "__builtin_ia32_vfmulcph512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 303 },
    { "__builtin_ia32_vfmulcsh_mask", CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 3856, 302 },
    { "__builtin_ia32_vpcomb", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 668 },
    { "__builtin_ia32_vpcomd", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 670 },
    { "__builtin_ia32_vpcomq", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 671 },
    { "__builtin_ia32_vpcomub", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 664 },
    { "__builtin_ia32_vpcomud", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 666 },
    { "__builtin_ia32_vpcomuq", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 667 },
    { "__builtin_ia32_vpcomuw", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 665 },
    { "__builtin_ia32_vpcomw", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 669 },
    // #126: the build's names that joined the own-main scope, from SemaX86.cpp's cases at the lines given.
    { "__builtin_ia32_alignd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 844 },
    { "__builtin_ia32_alignq512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 843 },
    { "__builtin_ia32_extracti64x4_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 566 },
    { "__builtin_ia32_gatherdiv8di", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 391 },
    { "__builtin_ia32_gathersiv16si", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 390 },
    { "__builtin_ia32_gathersiv8di", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 389 },
    { "__builtin_ia32_insert128i256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 581 },
    { "__builtin_ia32_inserti32x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 627 },
    { "__builtin_ia32_kshiftlidi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 823 },
    { "__builtin_ia32_kshiftlihi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 821 },
    { "__builtin_ia32_palignr128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 840 },
    { "__builtin_ia32_palignr256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 841 },
    { "__builtin_ia32_pblendw128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 836 },
    { "__builtin_ia32_permdi256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 744 },
    { "__builtin_ia32_prold512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 787 },
    { "__builtin_ia32_pshufd", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 751 },
    { "__builtin_ia32_pshufd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 752 },
    { "__builtin_ia32_pshufhw", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 754 },
    { "__builtin_ia32_pshuflw", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 757 },
    { "__builtin_ia32_psrldqi128_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 817 },
    { "__builtin_ia32_scatterdiv16si", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 416 },
    { "__builtin_ia32_scatterdiv8di", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 415 },
    { "__builtin_ia32_scattersiv16si", CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 278, 414 },
    { "__builtin_ia32_shuf_i32x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 853 },
    { "__builtin_ia32_shufps", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 856 },
    { "__builtin_ia32_shufpd", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 616 },
    { "__builtin_ia32_ucmpb128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 652 },
    { "__builtin_ia32_ucmpb256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 656 },
    { "__builtin_ia32_ucmpd128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 654 },
    { "__builtin_ia32_ucmpd256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 658 },
    { "__builtin_ia32_ucmpd512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 662 },
    { "__builtin_ia32_ucmpw512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 661 },
    { "__builtin_ia32_vec_ext_v16qi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 692 },
    { "__builtin_ia32_vec_ext_v4si", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 596 },
    { "__builtin_ia32_vec_ext_v8hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 7, 0, 608 },
    { "__builtin_ia32_vec_set_v4si", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 618 },
    { "__builtin_ia32_vec_set_v8hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 672 },
    // #148: the x86 builtins past the inventory (c_builtin_supplementary_scope.inc), by SemaX86.cpp too.
    { "__builtin_ia32_alignd128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 845 },
    { "__builtin_ia32_alignd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 846 },
    { "__builtin_ia32_alignq128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 847 },
    { "__builtin_ia32_alignq256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 848 },
    { "__builtin_ia32_blendpd", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 615 },
    { "__builtin_ia32_blendpd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 700 },
    { "__builtin_ia32_blendps", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 699 },
    { "__builtin_ia32_blendps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 838 },
    { "__builtin_ia32_dbpsadbw128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 859 },
    { "__builtin_ia32_dbpsadbw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 860 },
    { "__builtin_ia32_dbpsadbw512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 861 },
    { "__builtin_ia32_extract128i256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 564 },
    { "__builtin_ia32_extractf32x4_256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 571 },
    { "__builtin_ia32_extractf32x4_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 599 },
    { "__builtin_ia32_extractf32x8_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 567 },
    { "__builtin_ia32_extractf64x2_256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 569 },
    { "__builtin_ia32_extractf64x2_512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 601 },
    { "__builtin_ia32_extractf64x4_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 565 },
    { "__builtin_ia32_extracti32x4_256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 572 },
    { "__builtin_ia32_extracti32x8_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 568 },
    { "__builtin_ia32_extracti64x2_256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 570 },
    { "__builtin_ia32_extracti64x2_512_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 602 },
    { "__builtin_ia32_insertf32x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 626 },
    { "__builtin_ia32_insertf32x4_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 588 },
    { "__builtin_ia32_insertf32x8", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 582 },
    { "__builtin_ia32_insertf64x2_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 586 },
    { "__builtin_ia32_insertf64x2_512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 624 },
    { "__builtin_ia32_insertf64x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 584 },
    { "__builtin_ia32_inserti32x4_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 589 },
    { "__builtin_ia32_inserti32x8", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 583 },
    { "__builtin_ia32_inserti64x2_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 587 },
    { "__builtin_ia32_inserti64x2_512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 625 },
    { "__builtin_ia32_kshiftliqi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 820 },
    { "__builtin_ia32_kshiftlisi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 822 },
    { "__builtin_ia32_kshiftridi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 827 },
    { "__builtin_ia32_kshiftrihi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 825 },
    { "__builtin_ia32_kshiftriqi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 824 },
    { "__builtin_ia32_kshiftrisi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 826 },
    { "__builtin_ia32_palignr512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 842 },
    { "__builtin_ia32_pblendd128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 698 },
    { "__builtin_ia32_pblendd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 839 },
    { "__builtin_ia32_pblendw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 837 },
    { "__builtin_ia32_permdf256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 743 },
    { "__builtin_ia32_permdf512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 745 },
    { "__builtin_ia32_permdi512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 746 },
    { "__builtin_ia32_permti256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 835 },
    { "__builtin_ia32_prold128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 789 },
    { "__builtin_ia32_prold256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 790 },
    { "__builtin_ia32_prolq128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 791 },
    { "__builtin_ia32_prolq256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 792 },
    { "__builtin_ia32_prolq512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 788 },
    { "__builtin_ia32_prord128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 795 },
    { "__builtin_ia32_prord256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 796 },
    { "__builtin_ia32_prorq128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 797 },
    { "__builtin_ia32_prorq256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 798 },
    { "__builtin_ia32_prorq512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 794 },
    { "__builtin_ia32_pshufhw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 755 },
    { "__builtin_ia32_pshufhw512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 756 },
    { "__builtin_ia32_pshuflw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 758 },
    { "__builtin_ia32_pshuflw512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 759 },
    { "__builtin_ia32_pslldqi256_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 815 },
    { "__builtin_ia32_pslldqi512_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 816 },
    { "__builtin_ia32_psrldqi256_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 818 },
    { "__builtin_ia32_psrldqi512_byteshift", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 819 },
    { "__builtin_ia32_pternlogd128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 919 },
    { "__builtin_ia32_pternlogd128_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 920 },
    { "__builtin_ia32_pternlogd256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 921 },
    { "__builtin_ia32_pternlogd256_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 922 },
    { "__builtin_ia32_pternlogd512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 916 },
    { "__builtin_ia32_pternlogq128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 923 },
    { "__builtin_ia32_pternlogq128_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 924 },
    { "__builtin_ia32_pternlogq256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 925 },
    { "__builtin_ia32_pternlogq256_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 926 },
    { "__builtin_ia32_pternlogq512_maskz", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 918 },
    { "__builtin_ia32_shuf_f32x4", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 851 },
    { "__builtin_ia32_shuf_f32x4_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 620 },
    { "__builtin_ia32_shuf_f64x2", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 852 },
    { "__builtin_ia32_shuf_f64x2_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 621 },
    { "__builtin_ia32_shuf_i32x4_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 622 },
    { "__builtin_ia32_shuf_i64x2_256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 623 },
    { "__builtin_ia32_shufpd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 701 },
    { "__builtin_ia32_shufpd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 855 },
    { "__builtin_ia32_shufps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 857 },
    { "__builtin_ia32_shufps512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 858 },
    { "__builtin_ia32_ucmpq128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 655 },
    { "__builtin_ia32_ucmpq256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 659 },
    { "__builtin_ia32_ucmpw128_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 653 },
    { "__builtin_ia32_ucmpw256_mask", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 657 },
    { "__builtin_ia32_vec_ext_v16hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 693 },
    { "__builtin_ia32_vec_ext_v2di", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 560 },
    { "__builtin_ia32_vec_ext_v32qi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 31, 0, 719 },
    { "__builtin_ia32_vec_ext_v4hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 595 },
    { "__builtin_ia32_vec_ext_v4sf", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 597 },
    { "__builtin_ia32_vec_ext_v8si", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 7, 0, 609 },
    { "__builtin_ia32_vec_set_v16hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 714 },
    { "__builtin_ia32_vec_set_v16qi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 713 },
    { "__builtin_ia32_vec_set_v2di", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 577 },
    { "__builtin_ia32_vec_set_v32qi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 31, 0, 738 },
    { "__builtin_ia32_vec_set_v4di", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 619 },
    { "__builtin_ia32_vec_set_v4hi", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 617 },
    { "__builtin_ia32_vec_set_v8si", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 673 },
    { "__builtin_ia32_vinsertf128_pd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 578 },
    { "__builtin_ia32_vinsertf128_ps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 579 },
    { "__builtin_ia32_vinsertf128_si256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 580 },
    { "__builtin_ia32_vperm2f128_pd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 832 },
    { "__builtin_ia32_vperm2f128_ps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 833 },
    { "__builtin_ia32_vperm2f128_si256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 834 },
    { "__builtin_ia32_vpermilpd", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 594 },
    { "__builtin_ia32_vpermilpd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 678 },
    { "__builtin_ia32_vpermilpd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 749 },
    { "__builtin_ia32_vpermilps", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 747 },
    { "__builtin_ia32_vpermilps256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 748 },
    { "__builtin_ia32_vpermilps512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 750 },
    { "__builtin_ia32_vpshldd128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 862 },
    { "__builtin_ia32_vpshldd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 863 },
    { "__builtin_ia32_vpshldd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 864 },
    { "__builtin_ia32_vpshldq128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 865 },
    { "__builtin_ia32_vpshldq256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 866 },
    { "__builtin_ia32_vpshldq512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 867 },
    { "__builtin_ia32_vpshldw128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 868 },
    { "__builtin_ia32_vpshldw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 869 },
    { "__builtin_ia32_vpshldw512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 870 },
    { "__builtin_ia32_vpshrdd128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 871 },
    { "__builtin_ia32_vpshrdd256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 872 },
    { "__builtin_ia32_vpshrdd512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 873 },
    { "__builtin_ia32_vpshrdq128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 874 },
    { "__builtin_ia32_vpshrdq256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 875 },
    { "__builtin_ia32_vpshrdq512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 876 },
    { "__builtin_ia32_vpshrdw128", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 877 },
    { "__builtin_ia32_vpshrdw256", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 878 },
    { "__builtin_ia32_vpshrdw512", CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 879 },
};

static bool check_builtin_x86_test_expect(CheckBuiltinX86Test* test, bool condition, const char* description, uint32_t item)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-x86: %s item%u\n", description, item);
        }
        test->failures += 1;
    }
    return condition;
}

static bool check_builtin_x86_test_equal(const CheckBuiltinX86Result* a, const CheckBuiltinX86Result* b)
{
    bool equal = check_test_action_equal(&a->action, &b->action) && a->status == b->status && a->constraint == b->constraint &&
        a->checked_ice_arguments == b->checked_ice_arguments && a->matched_rules == b->matched_rules && a->unsupported == b->unsupported && a->range_event_count == b->range_event_count;
    for (uint32_t i = 0; i < a->range_event_count && i < 2; i += 1)
    {
        equal &= a->range_events[i].node == b->range_events[i].node && a->range_events[i].argument == b->range_events[i].argument &&
            a->range_events[i].low == b->range_events[i].low && a->range_events[i].high == b->range_events[i].high &&
            a->range_events[i].source_line == b->range_events[i].source_line && check_test_value_equal(&a->range_events[i].value, &b->range_events[i].value) &&
            a->range_events[i].disposition == b->range_events[i].disposition && a->range_events[i].default_error == b->range_events[i].default_error;
    }
    return equal;
}

static void check_builtin_x86_test_arguments(CheckValue arguments[BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS])
{
    for (uint32_t i = 0; i < BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS; i += 1)
    {
        arguments[i] = check_test_integer(BUSTER_TYPE_INT, 4);
        arguments[i].flags = 0;
    }
}

static CheckBuiltinX86Action check_builtin_x86_test_input(const char* name, const CheckValue* arguments, uint32_t count)
{
    CheckBuiltinX86Action input =
    {
        .name = name,
        .name_bytes = (uint32_t)strlen(name),
        .arguments = arguments,
        .argument_count = count,
        .ice_arguments = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1,
        .node = 148,
        .reachability = CHECK_BUILTIN_X86_UNKNOWN,
        .name_in_scope = true,
    };
    input.typed_call.value = check_test_integer(BUSTER_TYPE_UINT, 73);
    input.typed_call.value.flags = BUSTER_CHECK_SIDE_EFFECT;
    input.typed_call.value.fault = CHECK_FAULT_SHIFT_COUNT;
    input.typed_call.value.fault_node = 42;
    input.typed_call.conversions[0] = (CheckConversion){ .type = BUSTER_TYPE_UINT, .flags = 3 };
    input.typed_call.conversions[1] = (CheckConversion){ .type = BUSTER_TYPE_LONG, .flags = 5 };
    input.typed_call.store_conversion = (CheckConversion){ .type = BUSTER_TYPE_UCHAR, .flags = 7 };
    return input;
}

static void check_builtin_x86_test_facts_run(CheckBuiltinX86Test* test, const TypeTable* table)
{
    check_builtin_x86_test_expect(test, BUSTER_ARRAY_LENGTH(check_builtin_x86_test_facts) == 353, "independent primary fact count", 0);
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_builtin_x86_test_facts); f += 1)
    {
        const CheckBuiltinX86TestFact fact = check_builtin_x86_test_facts[f];
        CheckValue arguments[BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS];
        check_builtin_x86_test_arguments(arguments);
        CheckBuiltinX86Action input = check_builtin_x86_test_input(fact.name, arguments, 6);
        uint32_t rules = 0;
        for (uint32_t other = 0; other < BUSTER_ARRAY_LENGTH(check_builtin_x86_test_facts); other += 1)
        {
            const CheckBuiltinX86TestFact companion = check_builtin_x86_test_facts[other];
            if (strcmp(companion.name, fact.name) == 0)
            {
                arguments[companion.argument] = check_test_integer(BUSTER_TYPE_INT, companion.constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE ? companion.low : 4);
                arguments[companion.argument].flags = 0;
                input.required_ice_arguments |= UINT64_C(1) << companion.argument;
                rules += 1;
            }
        }
        const int64_t edges[] = { -1, (int64_t)fact.low - 1, fact.low, fact.high, (int64_t)fact.high + 1, INT32_MAX };
        const bool range = fact.constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE;
        for (uint32_t p = 0; p < (range ? BUSTER_ARRAY_LENGTH(edges) : 18); p += 1)
        {
            const int64_t value = range ? edges[p] : (int64_t)p - 1;
            arguments[fact.argument] = check_test_integer(BUSTER_TYPE_INT, (uint32_t)value);
            arguments[fact.argument].flags = 0;
            const bool accepted = range ? value >= fact.low && value <= fact.high : value >= 0 && value < 32 && (fact.accepted & (UINT32_C(1) << value)) != 0;
            CheckBuiltinX86Result result;
            check_builtin_x86_one(table, &input, &result);
            if (range || accepted)
            {
                check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && check_test_action_equal(&input.typed_call, &result.action) && result.matched_rules == rules &&
                    (result.checked_ice_arguments & input.required_ice_arguments) == input.required_ice_arguments, "fact success retains typed call and checks prototype ICE witnesses", f);
            }
            else
            {
                check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == fact.constraint && result.action.diagnostic != CHECK_DIAG_NONE &&
                    result.action.diagnostic_operand == fact.argument && result.range_event_count == 0, "hard RC/SAE set excludes all other small values", f);
            }
            if (range && !accepted)
            {
                check_builtin_x86_test_expect(test, result.range_event_count == 1 && result.range_events[0].node == input.node && result.range_events[0].argument == fact.argument &&
                    result.range_events[0].low == fact.low && result.range_events[0].high == fact.high && result.range_events[0].source_line == fact.source_line &&
                    result.range_events[0].disposition == CHECK_BUILTIN_X86_DEFERRED && result.range_events[0].default_error &&
                    check_test_value_equal(&result.range_events[0].value, arguments + fact.argument), "range event retains exact payload, bounds, primary line and default-error classification", f);
            }
            else if (range || accepted)
            {
                check_builtin_x86_test_expect(test, result.range_event_count == 0, "accepted immediate creates no range event", f);
            }
        }
    }
}

static void check_builtin_x86_test_contexts(CheckBuiltinX86Test* test, TypeTable* table)
{
    CheckValue arguments[BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS];
    check_builtin_x86_test_arguments(arguments);
    CheckBuiltinX86Action input = check_builtin_x86_test_input("__builtin_ia32_rndscaleps_mask", arguments, 5);
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 256);
    arguments[4] = check_test_integer(BUSTER_TYPE_INT, 4);
    for (uint32_t reachability = 0; reachability < 3; reachability += 1)
    {
        for (uint32_t suppressed = 0; suppressed < 2; suppressed += 1)
        {
            input.reachability = (CheckBuiltinX86Reachability)reachability;
            input.runtime_diagnostic_suppressed = suppressed != 0;
            CheckBuiltinX86Result result;
            check_builtin_x86_one(table, &input, &result);
            const CheckBuiltinX86EventDisposition expected = suppressed != 0 || reachability == CHECK_BUILTIN_X86_UNREACHABLE ? CHECK_BUILTIN_X86_SUPPRESSED :
                reachability == CHECK_BUILTIN_X86_REACHABLE ? CHECK_BUILTIN_X86_READY : CHECK_BUILTIN_X86_DEFERRED;
            check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && result.matched_rules == 2 && result.range_event_count == 1 &&
                result.range_events[0].argument == 1 && result.range_events[0].disposition == expected && result.range_events[0].default_error &&
                check_test_action_equal(&result.action, &input.typed_call), "dual-rule range event follows independent diagnostic context", reachability * 2 + suppressed);
        }
    }
    input.runtime_diagnostic_suppressed = true;
    input.constant_evaluated_context = true;
    arguments[1] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    input.ice_arguments &= ~(UINT64_C(1) << 1);
    CheckBuiltinX86Result result;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && result.range_event_count == 0, "constant context bypasses range-helper ICE and range check", 0);
    input.required_ice_arguments = UINT64_C(1) << 1;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE && result.action.diagnostic_operand == 1,
        "constant and unevaluated contexts never bypass prototype I marker", 0);
    input.required_ice_arguments = 0;
    arguments[4] = check_test_integer(BUSTER_TYPE_INT, 12);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK, "SAE accepts current-direction plus no-exception", 0);
    arguments[4] = check_test_integer(BUSTER_TYPE_INT, 9);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_SAE && result.action.diagnostic_operand == 4 && result.range_event_count == 0,
        "SAE hard check survives constant/unevaluated/unreachable suppression", 0);
    input.constant_evaluated_context = false;
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 256);
    input.required_ice_arguments = UINT64_C(1);
    input.ice_arguments &= ~UINT64_C(1);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE && result.action.diagnostic_operand == 0 && result.range_event_count == 0,
        "all prototype ICE constraints precede hard SAE and deferred ranges", 0);
    input.required_ice_arguments = 0;
    input.ice_arguments |= UINT64_C(1);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_SAE && result.range_event_count == 0,
        "SAE before range preserves original check order despite sorted manifest", 0);
    input.typed_call.diagnostic = CHECK_DIAG_INCOMPLETE;
    input.typed_call.diagnostic_operand = 3;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && check_test_action_equal(&input.typed_call, &result.action) && result.checked_ice_arguments == 0 && result.range_event_count == 0,
        "ordinary prototype diagnostic is retained before all extra checks", 0);
}

static void check_builtin_x86_test_witnesses(CheckBuiltinX86Test* test, TypeTable* table)
{
    CheckValue arguments[BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS];
    check_builtin_x86_test_arguments(arguments);
    CheckBuiltinX86Action input = check_builtin_x86_test_input("__builtin_ia32_addcarryx_u32", arguments, 64);
    input.required_ice_arguments = UINT64_C(1) | (UINT64_C(1) << 63);
    CheckBuiltinX86Result result;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && result.checked_ice_arguments == input.required_ice_arguments && result.matched_rules == 0 &&
        check_test_action_equal(&result.action, &input.typed_call), "64-bit ICE bitmap reaches argument63 without shift64 or synthetic result", 0);
    input.ice_arguments &= ~(UINT64_C(1) << 63);
    arguments[63].flags = BUSTER_CHECK_ICE;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE && result.action.diagnostic_operand == 63 &&
        result.checked_ice_arguments == input.required_ice_arguments, "CHECK_ICE flag cannot replace missing source witness at bit63", 0);
    input.ice_arguments = UINT64_MAX;
    arguments[63].flags = 0;
    const CheckValue bad[] =
    {
        { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_NONE },
        { .type = BUSTER_TYPE_DOUBLE, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_FLOATING, .floating = 4.0L },
        { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS },
        { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 4, .flags = BUSTER_CHECK_SIDE_EFFECT },
        { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 4, .fault = CHECK_FAULT_DIVIDE_BY_ZERO, .fault_node = 91 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad); i += 1)
    {
        arguments[63] = bad[i];
        check_builtin_x86_one(table, &input, &result);
        check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_INVALID && result.constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE && result.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.action.diagnostic_operand == 63,
            "source witness never overrides missing integer payload, noninteger kind, effects or faults", i);
    }
    arguments[63] = check_test_integer(type_bit_int(table, 257, false), 4);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_UNSUPPORTED && (result.unsupported & BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_WIDTH) != 0,
        "wide integer witness is explicit unsupported payload boundary", 0);
    arguments[63] = check_test_integer(BUSTER_TYPE_UCHAR, 256);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_UNSUPPORTED && (result.unsupported & BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_NORMALIZATION) != 0,
        "unnormalized narrow integer payload is unsupported rather than silently truncated", 0);
    arguments[63] = check_test_integer(BUSTER_TYPE_UINT128, (unsigned __int128)1 << 100);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && result.checked_ice_arguments == input.required_ice_arguments,
        "exact supported128-bit witness is never truncated to64 bits", 0);
    arguments[63] = check_test_integer(BUSTER_TYPE_INT, 4);
    input.ice_arguments = UINT64_C(1) << 63;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_ERROR && result.action.diagnostic_operand == 0 && result.checked_ice_arguments == 1,
        "prototype ICE failures are checked in ascending argument order", 0);
    input = check_builtin_x86_test_input("__builtin_ia32_addcarryx_u32", NULL, 0);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_OK && result.checked_ice_arguments == 0 && result.matched_rules == 0 &&
        check_test_action_equal(&input.typed_call, &result.action), "zero-size argument slice may be null", 0);
    input.name_in_scope = false;
    input.name = "__builtin_ia32_unrecognized148";
    input.name_bytes = (uint32_t)strlen(input.name);
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_UNSUPPORTED && (result.unsupported & BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_SCOPE) != 0,
        "unattested builtin name has explicit unsupported scope", 0);
    input.name_in_scope = true;
    input.name = "__builtin_ia32_addcarryx_u32";
    input.name_bytes = (uint32_t)strlen(input.name);
    input.argument_count = 65;
    check_builtin_x86_one(table, &input, &result);
    check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_UNSUPPORTED && (result.unsupported & BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_ARGUMENT_BOUND) != 0,
        "argument65 boundary is explicit unsupported before accessing the absent slice", 0);
    for (uint32_t i = 0; i < 5; i += 1)
    {
        input = check_builtin_x86_test_input("__builtin_ia32_addcarryx_u32", arguments, 1);
        if (i == 0)
        {
            input.required_ice_arguments = UINT64_C(1) << 1;
        }
        else if (i == 1)
        {
            input.ice_arguments |= UINT64_C(1) << 1;
        }
        else if (i == 2)
        {
            input.arguments = NULL;
        }
        else if (i == 3)
        {
            input.name = NULL;
        }
        else
        {
            input.reachability = (CheckBuiltinX86Reachability)UINT32_MAX;
        }
        check_builtin_x86_one(table, &input, &result);
        check_builtin_x86_test_expect(test, result.status == CHECK_BUILTIN_X86_INVALID, "malformed slices, bitmap bounds or reachability are invalid", i);
    }
}

static void check_builtin_x86_test_pages(CheckBuiltinX86Test* test, const TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((BUSTER_CHECK_BUILTIN_X86_TEST_BATCH * sizeof(CheckBuiltinX86Action) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((BUSTER_CHECK_BUILTIN_X86_TEST_BATCH * sizeof(CheckBuiltinX86Result) + 16 + page - 1) / page) * page : 0;
    const size_t argument_region = page != 0 ? ((BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t bytes = input_region + output_region + argument_region + 5 * page;
    uint8_t* mapping = page != 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    uint8_t* argument_end = ready ? output_end + page + argument_region : NULL;
    uint8_t* name_end = ready ? argument_end + 2 * page : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0 && mprotect(argument_end, page, PROT_NONE) == 0 &&
            mprotect(name_end, page, PROT_NONE) == 0;
    }
    check_builtin_x86_test_expect(test, ready, "guarded action/result/argument/name slices", 0);
    if (ready)
    {
        const char name[] = "__builtin_ia32_rndscaleps_mask";
        char* guarded_name = (char*)name_end - (sizeof(name) - 1);
        memcpy(guarded_name, name, sizeof(name) - 1);
        CheckValue* arguments = (CheckValue*)(argument_end - BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS * sizeof(*arguments));
        check_builtin_x86_test_arguments(arguments);
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, 256);
        arguments[4] = check_test_integer(BUSTER_TYPE_INT, 4);
        CheckValue original[BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS];
        memcpy(original, arguments, sizeof(original));
        for (uint32_t count = 0; count <= BUSTER_CHECK_BUILTIN_X86_TEST_BATCH; count += 1)
        {
            CheckBuiltinX86Action* actions = (CheckBuiltinX86Action*)(input_end - count * sizeof(*actions));
            CheckBuiltinX86Result* results = (CheckBuiltinX86Result*)(output_end - count * sizeof(*results));
            CheckBuiltinX86Result expected[BUSTER_CHECK_BUILTIN_X86_TEST_BATCH];
            memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
            memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
            for (uint32_t i = 0; i < count; i += 1)
            {
                actions[i] = check_builtin_x86_test_input(name, arguments, BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS);
                actions[i].name = guarded_name;
                actions[i].node += i;
                actions[i].reachability = (CheckBuiltinX86Reachability)(i % 3);
                actions[i].constant_evaluated_context = i % 5 == 0;
                actions[i].required_ice_arguments = UINT64_C(1) << 63;
                if (i % 7 == 0)
                {
                    actions[i].ice_arguments &= ~(UINT64_C(1) << 63);
                }
                check_builtin_x86_one(table, actions + i, expected + i);
            }
            for (uint32_t partition = 1; partition <= 5; partition += 1)
            {
                for (uint32_t first = 0; first < count; first += partition)
                {
                    const uint32_t remaining = count - first < partition ? count - first : partition;
                    check_builtin_x86(table, actions + first, results + first, remaining);
                }
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_builtin_x86_test_expect(test, check_builtin_x86_test_equal(expected + i, results + i), "guarded legal partitions preserve all diagnostics and event payloads", i);
                }
            }
            check_builtin_x86(table, actions, results, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_x86_test_expect(test, check_builtin_x86_test_equal(expected + i, results + i), "guarded full batch equals independent individual actions", i);
            }
            for (uint32_t i = 1; i <= 16; i += 1)
            {
                check_builtin_x86_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5, "batch leading sentinels unchanged", i);
            }
            // By field: the values' padding is no part of them.
            bool unchanged = true;
            for (uint32_t i = 0; i < BUSTER_CHECK_BUILTIN_X86_TEST_ARGUMENTS; i += 1)
            {
                unchanged &= check_test_value_equal(original + i, arguments + i);
            }
            check_builtin_x86_test_expect(test, unchanged, "read-only argument slice remains unchanged", count);
        }
        check_builtin_x86_test_expect(test, memcmp(guarded_name, name, sizeof(name) - 1) == 0, "non-NUL exact name remains unchanged", 0);
    }
    check_builtin_x86(NULL, NULL, NULL, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

// The shared parser oracle intentionally uses -w. Runtime range diagnostics are Warning+DefaultError and need an
// independent invocation without that blanket suppression, otherwise the reachability diagnostic would vanish.
static bool check_builtin_x86_test_clang_one(const char* clang, const char* path, uint32_t policy, bool* rejected, char diagnostics[4096])
{
    char* arguments[12];
    uint32_t count = 0;
    arguments[count++] = (char*)clang;
    arguments[count++] = "-fsyntax-only";
    arguments[count++] = "-std=gnu17";
    arguments[count++] = "-fno-caret-diagnostics";
    arguments[count++] = "-fno-color-diagnostics";
    arguments[count++] = "-ferror-limit=0";
    if (policy != 0)
    {
        arguments[count++] = policy == 1 ? "-Wno-error=argument-outside-range" : "-Wno-argument-outside-range";
    }
    arguments[count++] = (char*)path;
    arguments[count] = NULL;
    OracleRun run = {
        .purpose = "checker x86 range diagnostic oracle",
        .arguments = arguments,
        .dependencies = true,
        .timeout = 10,
    };
    const int status = oracle_test_run(&run, 0, 0, diagnostics, 4096);
    const bool ready = status == 0 || status == 1;
    *rejected = ready && status == 1;
    return ready;
}

#define BUSTER_CHECK_BUILTIN_X86_TEST_TYPES "typedef char C16 __attribute__((vector_size(16))); typedef char C32 __attribute__((vector_size(32))); typedef long long Q4 __attribute__((vector_size(32))); typedef double D2 __attribute__((vector_size(16))); typedef double D8 __attribute__((vector_size(64))); typedef float F16 __attribute__((vector_size(64))); typedef int I8 __attribute__((vector_size(32))); typedef int I16 __attribute__((vector_size(64)));\n"
#define BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "__attribute__((target(\"avx512f,avx512bw,avx512vl\"))) "
// The 128-bit vectors of #148's SSE builtins, then the target.
#define BUSTER_CHECK_BUILTIN_X86_TEST_SSE BUSTER_CHECK_BUILTIN_X86_TEST_TYPES \
    "typedef int I4 __attribute__((vector_size(16))); typedef short S8 __attribute__((vector_size(16))); typedef float F4 __attribute__((vector_size(16)));\n" \
    BUSTER_CHECK_BUILTIN_X86_TEST_TARGET

static void check_builtin_x86_test_clang(CheckBuiltinX86Test* test, const char* clang)
{
    static const struct
    {
        const char* source;
        uint32_t policy;
        bool rejected;
        const char* diagnostic;
        const char* excluded;
    } cases[] =
    {
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a){return __builtin_ia32_cmpb128_mask(a,a,7,65535);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n", 0, true, "error: argument value 8 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n", 1, false, "warning: argument value 8 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a){return __builtin_ia32_cmpb128_mask(a,a,8,65535);}\n", 2, false, NULL, "outside the valid range" },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a){if(0)return __builtin_ia32_cmpb128_mask(a,a,8,65535);return 0;}\n", 0, false, NULL, "outside the valid range" },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned long f(C16 a){return sizeof(__builtin_ia32_cmpb128_mask(a,a,8,65535));}\n", 0, false, NULL, "outside the valid range" },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a,int x){return __builtin_ia32_cmpb128_mask(a,a,x,65535);}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(C16 a,int x){if(0)return __builtin_ia32_cmpb128_mask(a,a,x,65535);return 0;}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned long f(C16 a,int x){return sizeof(__builtin_ia32_cmpb128_mask(a,a,x,65535));}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I8 f(D8 a,I8 old){return __builtin_ia32_cvtpd2dq512_mask(a,old,255,4);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I8 f(D8 a,I8 old){return __builtin_ia32_cvtpd2dq512_mask(a,old,255,11);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I8 f(D8 a,I8 old){return __builtin_ia32_cvtpd2dq512_mask(a,old,255,12);}\n", 0, true, "invalid rounding argument", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I8 f(D8 a,I8 old){return __builtin_ia32_cvtpd2dq512_mask(a,old,255,0);}\n", 0, true, "invalid rounding argument", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "int f(D2 a){return __builtin_ia32_vcvttsd2si32(a,12);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "int f(D2 a){return __builtin_ia32_vcvttsd2si32(a,9);}\n", 0, true, "invalid rounding argument", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "int f(D2 a){return sizeof(__builtin_ia32_vcvttsd2si32(a,9));}\n", 0, true, "invalid rounding argument", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "int f(D2 a){if(0)return __builtin_ia32_vcvttsd2si32(a,9);return 0;}\n", 0, true, "invalid rounding argument", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "F16 f(F16 a){return __builtin_ia32_rndscaleps_mask(a,255,a,65535,12);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "F16 f(F16 a){return __builtin_ia32_rndscaleps_mask(a,256,a,65535,4);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "F16 f(F16 a){return __builtin_ia32_rndscaleps_mask(a,256,a,65535,9);}\n", 0, true, "invalid rounding argument", "argument value 256" },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "F16 f(F16 a,int x){return __builtin_ia32_rndscaleps_mask(a,x,a,65535,9);}\n", 0, true, "must be a constant integer", "invalid rounding argument" },
        // #126's scales, a hard set wherever the call is, and one of its ranges.
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I16 f(I16 i,const void* p){return __builtin_ia32_gathersiv16si(i,p,i,65535,8);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I16 f(I16 i,const void* p){return __builtin_ia32_gathersiv16si(i,p,i,65535,3);}\n", 0, true, "scale argument must be 1, 2, 4, or 8", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "I16 f(I16 i,const void* p){if(0)return __builtin_ia32_gathersiv16si(i,p,i,65535,3);return i;}\n", 0, true, "scale argument must be 1, 2, 4, or 8", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned long f(I16 i,const void* p){return sizeof(__builtin_ia32_gathersiv16si(i,p,i,65535,3));}\n", 0, true, "scale argument must be 1, 2, 4, or 8", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(I16 a){return __builtin_ia32_ucmpd512_mask(a,a,7,65535);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned short f(I16 a){return __builtin_ia32_ucmpd512_mask(a,a,8,65535);}\n", 0, true, "error: argument value 8 is outside the valid range", NULL },
        // #619's: _mm_cmp_epu32_mask's builtin, which c_check_program_batch.c reaches through _mm_cmplt_epu32_mask.
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES "typedef int I4 __attribute__((vector_size(16)));\n" BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned char f(I4 a){return __builtin_ia32_ucmpd128_mask(a,a,1,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_TYPES "typedef int I4 __attribute__((vector_size(16)));\n" BUSTER_CHECK_BUILTIN_X86_TEST_TARGET "unsigned char f(I4 a){return __builtin_ia32_ucmpd128_mask(a,a,8,255);}\n", 0, true, "error: argument value 8 is outside the valid range", NULL },
        // #148's SSE shuffles, byte shift, align, extract and insert, which stb_image, dr_libs and minimp3 call: each at
        // its range's top and one past it.
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "I4 f(I4 a){return __builtin_ia32_pshufd(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "I4 f(I4 a){return __builtin_ia32_pshufd(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a){return __builtin_ia32_pshufhw(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a){return __builtin_ia32_pshufhw(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a){return __builtin_ia32_pshuflw(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a){return __builtin_ia32_pshuflw(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "F4 f(F4 a,F4 b){return __builtin_ia32_shufps(a,b,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "F4 f(F4 a,F4 b){return __builtin_ia32_shufps(a,b,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C16 f(C16 a){return __builtin_ia32_psrldqi128_byteshift(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C16 f(C16 a){return __builtin_ia32_psrldqi128_byteshift(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C16 f(C16 a,C16 b){return __builtin_ia32_palignr128(a,b,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C16 f(C16 a,C16 b){return __builtin_ia32_palignr128(a,b,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "short f(S8 a){return __builtin_ia32_vec_ext_v8hi(a,7);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "short f(S8 a){return __builtin_ia32_vec_ext_v8hi(a,8);}\n", 0, true, "error: argument value 8 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a,short v){return __builtin_ia32_vec_set_v8hi(a,v,7);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a,short v){return __builtin_ia32_vec_set_v8hi(a,v,8);}\n", 0, true, "error: argument value 8 is outside the valid range", NULL },
        // #799's separately attested immediate forms: top of range, first out-of-range, and a nonconstant.
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "D2 f(D2 a,D2 b){return __builtin_ia32_shufpd(a,b,3);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "D2 f(D2 a,D2 b){return __builtin_ia32_shufpd(a,b,4);}\n", 0, true, "error: argument value 4 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "D2 f(D2 a,D2 b,int i){return __builtin_ia32_shufpd(a,b,i);}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a,S8 b){return __builtin_ia32_pblendw128(a,b,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a,S8 b){return __builtin_ia32_pblendw128(a,b,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "S8 f(S8 a,S8 b,int i){return __builtin_ia32_pblendw128(a,b,i);}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "I8 f(I8 a){return __builtin_ia32_pshufd256(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "I8 f(I8 a){return __builtin_ia32_pshufd256(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "I8 f(I8 a,int i){return __builtin_ia32_pshufd256(a,i);}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "Q4 f(Q4 a){return __builtin_ia32_permdi256(a,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "Q4 f(Q4 a){return __builtin_ia32_permdi256(a,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "Q4 f(Q4 a,int i){return __builtin_ia32_permdi256(a,i);}\n", 0, true, "must be a constant integer", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C32 f(C32 a,C32 b){return __builtin_ia32_palignr256(a,b,255);}\n", 0, false, NULL, NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C32 f(C32 a,C32 b){return __builtin_ia32_palignr256(a,b,256);}\n", 0, true, "error: argument value 256 is outside the valid range", NULL },
        { BUSTER_CHECK_BUILTIN_X86_TEST_SSE "C32 f(C32 a,C32 b,int i){return __builtin_ia32_palignr256(a,b,i);}\n", 0, true, "must be a constant integer", NULL },
    };
    char directory[] = "/tmp/buster-check-x86-XXXXXX";
    const bool ready = mkdtemp(directory) != NULL;
    check_builtin_x86_test_expect(test, ready, "Clang source fixture directory", 0);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char path[160];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        const bool written = parse_test_write(path, cases[i].source, strlen(cases[i].source));
        char diagnostics[4096] = { 0 };
        bool rejected = false;
        const bool ran = written && check_builtin_x86_test_clang_one(clang, path, cases[i].policy, &rejected, diagnostics);
        const bool matched = ran && rejected == cases[i].rejected && (cases[i].diagnostic == NULL || strstr(diagnostics, cases[i].diagnostic) != NULL) &&
            (cases[i].excluded == NULL || strstr(diagnostics, cases[i].excluded) == NULL);
        if (!matched && test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-x86: Clang fixture%u rejected=%u/%u diagnostics=%s\n", i, rejected, cases[i].rejected, diagnostics);
        }
        check_builtin_x86_test_expect(test, matched, "independent Clang immediate/context/severity constraint source", i);
        unlink(path);
    }
    if (ready)
    {
        rmdir(directory);
    }
}

#undef BUSTER_CHECK_BUILTIN_X86_TEST_SSE
#undef BUSTER_CHECK_BUILTIN_X86_TEST_TYPES
#undef BUSTER_CHECK_BUILTIN_X86_TEST_TARGET

// A fact's value of its constrained argument that clang takes: a range's from its bottom to its top, a set's members.
static bool check_builtin_x86_test_accepts(const CheckBuiltinX86TestFact* fact, int64_t value)
{
    const bool result = fact->constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE ? value >= fact->low && value <= fact->high :
                                                                                 value >= 0 && value < 32 && (fact->accepted & (UINT32_C(1) << value)) != 0;
    return result;
}

// The supplementary scope's facts against clang itself (#148): for each fact of a name the scope holds, a function per value
// of its constrained argument, which calls the builtin with the function's parameters as its other arguments, declared at
// the prototype's types (clang's metadata decoded, spelled as clang spells them: type_print), the name's other facts'
// arguments at values they accept and its other ICEs 0. A range's values are its edges, its bottom and top and one past
// each; a set's are -1 to 16. Clang must report an error on each line whose value the fact refuses, and on no other: a
// range's with its bounds (warn_argument_invalid_range, an error by default), a set's as an invalid rounding argument or
// scale. So each fact read from SemaX86.cpp is clang's, its argument's index too.
static void check_builtin_x86_test_clang_supplementary(CheckBuiltinX86Test* test, TypeTable* table, const char* clang)
{
    const size_t capacity = (size_t)1 << 22;
    char* source = malloc(capacity);
    char* diagnostics = malloc(capacity);
    // Each line's case: its fact's index and its value, the first line the file's own.
    uint32_t* lines = malloc(sizeof(uint32_t) * 8192);
    int64_t* values = malloc(sizeof(int64_t) * 8192);
    bool ready = source != NULL && diagnostics != NULL && lines != NULL && values != NULL;
    check_builtin_x86_test_expect(test, ready, "supplementary facts' Clang source memory", 0);
    size_t used = 0;
    uint32_t line_count = 1;
    uint32_t facts = 0;
    Arena text = arena_reserve(UINT64_C(1) << 20);
    ready = ready && text.base != NULL;
    for (uint32_t f = 0; ready && f < BUSTER_ARRAY_LENGTH(check_builtin_x86_test_facts); f += 1)
    {
        const CheckBuiltinX86TestFact* fact = check_builtin_x86_test_facts + f;
        const uint32_t length = (uint32_t)strlen(fact->name);
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_supplementary_in_scope(fact->name, length) ? builtin_intrinsic_lookup(fact->name, length) : NULL;
        BuiltinIntrinsicTypeResult types = { 0 };
        if (info != NULL)
        {
            const BuiltinIntrinsicTypeInput input = {
                .encoding = info->encoding,
                .encoding_length = info->encoding_length,
                .attributes = info->attributes,
                .attributes_length = info->attributes_length,
                .custom_typechecking = info->custom_typechecking,
            };
            builtin_intrinsic_types_decode(table, &input, &types);
            check_builtin_x86_test_expect(test, types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && fact->argument < types.parameter_count,
                                          "a supplementary fact's prototype decodes with its argument", f);
        }
        const bool range = fact->constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE;
        const int64_t range_values[] = { (int64_t)fact->low - 1, fact->low, fact->high, (int64_t)fact->high + 1 };
        const uint32_t value_count = range ? 4 : 18;
        for (uint32_t v = 0; info != NULL && types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && fact->argument < types.parameter_count && v < value_count;
             v += 1)
        {
            const int64_t value = range ? range_values[v] : (int64_t)v - 1;
            ready = ready && line_count < 8192 && used + 4096 < capacity;
            if (ready)
            {
                used += (size_t)snprintf(source + used, capacity - used, "void f%u(", line_count);
                for (uint32_t p = 0; p < types.parameter_count; p += 1)
                {
                    char name[16];
                    snprintf(name, sizeof(name), "p%u", p);
                    const String8 printed = type_print(table, &text, types.parameters[p], (String8){ .pointer = name, .length = strlen(name) }, 0, 0);
                    used += (size_t)snprintf(source + used, capacity - used, "%s%.*s", p != 0 ? "," : "", (int)printed.length, printed.pointer);
                    arena_pop_to(&text, 0);
                }
                used += (size_t)snprintf(source + used, capacity - used, "){(void)%s(", fact->name);
                for (uint32_t p = 0; p < types.parameter_count; p += 1)
                {
                    // The fact's argument takes the value, another fact's of the name a value it accepts, another ICE 0.
                    int64_t constant = (types.ice_parameters & (UINT64_C(1) << p)) != 0 ? 0 : -2;
                    for (uint32_t other = 0; other < BUSTER_ARRAY_LENGTH(check_builtin_x86_test_facts); other += 1)
                    {
                        const CheckBuiltinX86TestFact* companion = check_builtin_x86_test_facts + other;
                        if (strcmp(companion->name, fact->name) == 0 && companion->argument == p)
                        {
                            constant = companion->constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE ? (int64_t)companion->low :
                                       companion->constraint == CHECK_BUILTIN_X86_CONSTRAINT_SCALE ? 1 : 4;
                        }
                    }
                    constant = p == fact->argument ? value : constant;
                    if (constant == -2 && p != fact->argument)
                    {
                        used += (size_t)snprintf(source + used, capacity - used, "%sp%u", p != 0 ? "," : "", p);
                    }
                    else
                    {
                        used += (size_t)snprintf(source + used, capacity - used, "%s%lld", p != 0 ? "," : "", (long long)constant);
                    }
                }
                used += (size_t)snprintf(source + used, capacity - used, ");}\n");
                lines[line_count] = f;
                values[line_count] = value;
                line_count += 1;
            }
        }
        facts += info != NULL;
    }
    char directory[] = "/tmp/buster-check-x86-supplementary-XXXXXX";
    ready = ready && mkdtemp(directory) != NULL;
    char path[160];
    snprintf(path, sizeof(path), "%s/facts.c", directory);
    ready = ready && parse_test_write(path, source, used);
    char* arguments[] = { (char*)clang, "-fsyntax-only", "-std=gnu17", "-march=znver5", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                          "-ferror-limit=0", path, NULL };
    OracleRun run = {
        .purpose = "checker x86 supplementary facts oracle",
        .arguments = arguments,
        .dependencies = true,
        .timeout = 60,
    };
    const int status = ready ? oracle_test_run(&run, 0, 0, diagnostics, capacity) : -1;
    ready = ready && (status == 0 || status == 1);
    check_builtin_x86_test_expect(test, ready, "supplementary facts' Clang run", 0);
    // The lines with an error, each against its case: refused exactly where the fact refuses, with the fact's message.
    uint8_t* reported = calloc(line_count + 1, 1);
    ready = ready && reported != NULL;
    const size_t path_length = strlen(path);
    for (const char* cursor = ready ? diagnostics : NULL; cursor != NULL && *cursor != 0;)
    {
        const char* end = strchr(cursor, '\n');
        const size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length > path_length + 1 && memcmp(cursor, path, path_length) == 0 && cursor[path_length] == ':')
        {
            char* after = NULL;
            const unsigned long line = strtoul(cursor + path_length + 1, &after, 10);
            const char* error = strstr(cursor, ": error: ");
            if (line > 0 && line < line_count && error != NULL && error < cursor + length)
            {
                const CheckBuiltinX86TestFact* fact = check_builtin_x86_test_facts + lines[line];
                char expected[96];
                if (fact->constraint == CHECK_BUILTIN_X86_CONSTRAINT_RANGE)
                {
                    snprintf(expected, sizeof(expected), "is outside the valid range [%u, %u]", fact->low, fact->high);
                }
                else
                {
                    snprintf(expected, sizeof(expected), "%s", fact->constraint == CHECK_BUILTIN_X86_CONSTRAINT_SCALE ? "scale argument must be" : "invalid rounding argument");
                }
                const char* found = strstr(error, expected);
                reported[line] |= found != NULL && found < cursor + length ? 1 : 2;
            }
        }
        cursor = end != NULL ? end + 1 : NULL;
    }
    uint32_t mismatched = 0;
    for (uint32_t line = 1; ready && line < line_count; line += 1)
    {
        const CheckBuiltinX86TestFact* fact = check_builtin_x86_test_facts + lines[line];
        const bool refused = !check_builtin_x86_test_accepts(fact, values[line]);
        const bool right = refused ? reported[line] == 1 : reported[line] == 0;
        if (!right && mismatched < 8)
        {
            fprintf(stderr, "test-check-builtins-x86: supplementary fact %s at %lld: clang %s\n", fact->name, (long long)values[line],
                    reported[line] == 0 ? "accepts it" : reported[line] == 1 ? "refuses it" : "refuses it otherwise");
        }
        mismatched += !right;
    }
    char description[160];
    snprintf(description, sizeof(description), "the supplementary scope's %u facts are clang's (%u cases, %u not)", facts, line_count - 1, mismatched);
    check_builtin_x86_test_expect(test, ready && facts != 0 && mismatched == 0, description, 0);
    if (ready)
    {
        unlink(path);
        rmdir(directory);
    }
    arena_release(&text);
    free(reported);
    free(values);
    free(lines);
    free(diagnostics);
    free(source);
}

static int run_check_builtin_x86_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinX86Test test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            i += 1;
            clang = argv[i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_x86_test_expect(&test, options, "usage: test-check-builtins-x86 [--clang PATH]", 0);
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_x86_test_expect(&test, initialized, "type-table allocation", 0);
        if (initialized)
        {
            check_builtin_x86_test_facts_run(&test, &types.table);
            check_builtin_x86_test_contexts(&test, &types.table);
            check_builtin_x86_test_witnesses(&test, &types.table);
            check_builtin_x86_test_pages(&test, &types.table);
            check_builtin_x86_test_clang_supplementary(&test, &types.table, clang);
        }
        check_test_types_close(&types);
        check_builtin_x86_test_clang(&test, clang);
    }
    printf("test-check-builtins-x86: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    oracle_cache_report(oracle_cache(), "test-check-builtins-x86");
    oracle_epoch_end(epoch);
    return test.failures != 0;
}
