// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared helper identities and native records. BUSTER_CBUILTIN_X86_LEAF takes the stable helper ID and
// its record fields; the preprocessor discards every field except the name. No encoder types are needed there.

#define BUSTER_CBUILTIN_X86_HELPER(name, parameters, result, records, features) \
    name, sizeof(name) - 1, parameters, result, records, BUSTER_ARRAY_LENGTH(records), features, "x86 builtins whose instructions the target hasn't", 0, false, 0
// A leaf of an extension's instructions, with its vectors and its branch.
#define BUSTER_CBUILTIN_X86_EXTENSION_HELPER(name, parameters, result, records, extensions, vectors, branch_count, branch, target) \
    name, sizeof(name) - 1, parameters, result, records, BUSTER_ARRAY_LENGTH(records), 0, "x86 builtins whose instructions the target hasn't", 0, false, \
      vectors, extensions, branch_count, branch, target

    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDTSC, BUSTER_CBUILTIN_X86_HELPER("__rdtsc", 0, true, cbuiltin_x86_helper_rdtsc, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDTSC_BUILTIN, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_rdtsc", 0, true, cbuiltin_x86_helper_rdtsc, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LZCNT16, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_lzcnt_u16", 1, true, cbuiltin_x86_helper_lzcnt16, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LFENCE, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_lfence", 0, false, cbuiltin_x86_helper_lfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MFENCE, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_mfence", 0, false, cbuiltin_x86_helper_mfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_SFENCE, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_sfence", 0, false, cbuiltin_x86_helper_sfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_CLFLUSH, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_clflush", 1, false, cbuiltin_x86_helper_clflush, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LDMXCSR, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_ldmxcsr", 1, false, cbuiltin_x86_helper_ldmxcsr, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_STMXCSR, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_stmxcsr", 0, true, cbuiltin_x86_helper_stmxcsr, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSETBV, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsetbv", 2, false, cbuiltin_x86_helper_xsetbv, BUSTER_X86_FEATURE(XSAVE)))
    // The header builtins (#148): the names <xmmintrin.h>, <emmintrin.h> and <prfchwintrin.h> declare, which clang's
    // code compiles as the builtins they are.
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_LFENCE, BUSTER_CBUILTIN_X86_HELPER("_mm_lfence", 0, false, cbuiltin_x86_helper_lfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_MFENCE, BUSTER_CBUILTIN_X86_HELPER("_mm_mfence", 0, false, cbuiltin_x86_helper_mfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_SFENCE, BUSTER_CBUILTIN_X86_HELPER("_mm_sfence", 0, false, cbuiltin_x86_helper_sfence, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_PAUSE, BUSTER_CBUILTIN_X86_HELPER("_mm_pause", 0, false, cbuiltin_x86_helper_pause, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_CLFLUSH, BUSTER_CBUILTIN_X86_HELPER("_mm_clflush", 1, false, cbuiltin_x86_helper_clflush, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_GETCSR, BUSTER_CBUILTIN_X86_HELPER("_mm_getcsr", 0, true, cbuiltin_x86_helper_stmxcsr, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MM_SETCSR, BUSTER_CBUILTIN_X86_HELPER("_mm_setcsr", 1, false, cbuiltin_x86_helper_ldmxcsr, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_M_PREFETCH, BUSTER_CBUILTIN_X86_HELPER("_m_prefetch", 1, false, cbuiltin_x86_helper_prefetch, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_M_PREFETCHW, BUSTER_CBUILTIN_X86_HELPER("_m_prefetchw", 1, false, cbuiltin_x86_helper_prefetchw, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_PAUSE, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_pause", 0, false, cbuiltin_x86_helper_pause, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XGETBV, "__builtin_ia32_xgetbv", sizeof("__builtin_ia32_xgetbv") - 1, 1, true, cbuiltin_x86_helper_xgetbv,
                                     BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_xgetbv), BUSTER_X86_FEATURE(XSAVE), "xgetbv without XSAVE", 0, false, false)
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MONITORX, "__builtin_ia32_monitorx", sizeof("__builtin_ia32_monitorx") - 1, 3, false, cbuiltin_x86_helper_monitorx,
                                       BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_monitorx), BUSTER_X86_FEATURE(MWAITX), "monitorx or mwaitx without MWAITX", 0, false, false)
    // mwaitx's leaf saves rbx (Tier0Frame.saved's first register) around its body.
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MWAITX, "__builtin_ia32_mwaitx", sizeof("__builtin_ia32_mwaitx") - 1, 3, false, cbuiltin_x86_helper_mwaitx,
                                     BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_mwaitx), BUSTER_X86_FEATURE(MWAITX), "monitorx or mwaitx without MWAITX", 1, false, false)
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_TZCNT16, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u16", 1, true, cbuiltin_x86_helper_tzcnt16, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_TZCNT32, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u32", 1, true, cbuiltin_x86_helper_tzcnt32, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_TZCNT64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_tzcnt_u64", 1, true, cbuiltin_x86_helper_tzcnt64, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LZCNT32, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_lzcnt_u32", 1, true, cbuiltin_x86_helper_lzcnt32, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LZCNT64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_lzcnt_u64", 1, true, cbuiltin_x86_helper_lzcnt64, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATELEFT8, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateleft8", 2, true, cbuiltin_x86_helper_rotateleft8, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATELEFT16, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateleft16", 2, true, cbuiltin_x86_helper_rotateleft16, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATELEFT32, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateleft32", 2, true, cbuiltin_x86_helper_rotateleft32, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATELEFT64, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateleft64", 2, true, cbuiltin_x86_helper_rotateleft64, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATERIGHT8, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateright8", 2, true, cbuiltin_x86_helper_rotateright8, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATERIGHT16, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateright16", 2, true, cbuiltin_x86_helper_rotateright16, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATERIGHT32, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateright32", 2, true, cbuiltin_x86_helper_rotateright32, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ROTATERIGHT64, BUSTER_CBUILTIN_X86_HELPER("__builtin_rotateright64", 2, true, cbuiltin_x86_helper_rotateright64, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVE, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsave", 2, false, cbuiltin_x86_helper_xsave, BUSTER_X86_FEATURE(XSAVE)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVE64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsave64", 2, false, cbuiltin_x86_helper_xsave64, BUSTER_X86_FEATURE(XSAVE)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XRSTOR, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xrstor", 2, false, cbuiltin_x86_helper_xrstor, BUSTER_X86_FEATURE(XSAVE)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XRSTOR64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xrstor64", 2, false, cbuiltin_x86_helper_xrstor64, BUSTER_X86_FEATURE(XSAVE)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVEC, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsavec", 2, false, cbuiltin_x86_helper_xsavec, BUSTER_X86_FEATURE(XSAVEC)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVEC64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsavec64", 2, false, cbuiltin_x86_helper_xsavec64, BUSTER_X86_FEATURE(XSAVEC)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVEOPT, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsaveopt", 2, false, cbuiltin_x86_helper_xsaveopt, BUSTER_X86_FEATURE(XSAVEOPT)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVEOPT64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsaveopt64", 2, false, cbuiltin_x86_helper_xsaveopt64,
                                                                  BUSTER_X86_FEATURE(XSAVEOPT)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVES, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsaves", 2, false, cbuiltin_x86_helper_xsaves, BUSTER_X86_FEATURE(XSAVES)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XSAVES64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xsaves64", 2, false, cbuiltin_x86_helper_xsaves64, BUSTER_X86_FEATURE(XSAVES)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XRSTORS, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xrstors", 2, false, cbuiltin_x86_helper_xrstors, BUSTER_X86_FEATURE(XSAVES)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XRSTORS64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_xrstors64", 2, false, cbuiltin_x86_helper_xrstors64, BUSTER_X86_FEATURE(XSAVES)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_FXRSTOR, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_fxrstor", 1, false, cbuiltin_x86_helper_fxrstor, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_FXRSTOR64, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_fxrstor64", 1, false, cbuiltin_x86_helper_fxrstor64, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDPKRU, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_rdpkru", 0, true, cbuiltin_x86_helper_rdpkru, BUSTER_X86_FEATURE(PKU)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_WRPKRU, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_wrpkru", 1, false, cbuiltin_x86_helper_wrpkru, BUSTER_X86_FEATURE(PKU)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDPMC, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_rdpmc", 1, true, cbuiltin_x86_helper_rdpmc, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDPRU, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_rdpru", 1, true, cbuiltin_x86_helper_rdpru, BUSTER_X86_FEATURE(RDPRU)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_RDTSCP, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_rdtscp", 1, true, cbuiltin_x86_helper_rdtscp, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_READEFLAGS, "__builtin_ia32_readeflags_u64", sizeof("__builtin_ia32_readeflags_u64") - 1, 0, true, cbuiltin_x86_helper_readeflags,
                                         BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_readeflags), 0, "x86 builtins whose instructions the target hasn't", 0, true, false)
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_WRITEEFLAGS, "__builtin_ia32_writeeflags_u64", sizeof("__builtin_ia32_writeeflags_u64") - 1, 1, false, cbuiltin_x86_helper_writeeflags,
                                          BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_writeeflags), 0, "x86 builtins whose instructions the target hasn't", 0, true, false)
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_CLZERO, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_clzero", 1, false, cbuiltin_x86_helper_clzero, BUSTER_X86_FEATURE(CLZERO)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MONITOR, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_monitor", 3, false, cbuiltin_x86_helper_monitor, BUSTER_X86_FEATURE(SSE3)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_MWAIT, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_mwait", 2, false, cbuiltin_x86_helper_mwait, BUSTER_X86_FEATURE(SSE3)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_VZEROALL, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_vzeroall", 0, false, cbuiltin_x86_helper_vzeroall, BUSTER_X86_FEATURE(AVX)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_VZEROUPPER, BUSTER_CBUILTIN_X86_HELPER("__builtin_ia32_vzeroupper", 0, false, cbuiltin_x86_helper_vzeroupper, BUSTER_X86_FEATURE(AVX)))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_SHA256RNDS2, "__builtin_ia32_sha256rnds2", sizeof("__builtin_ia32_sha256rnds2") - 1, 3, true, cbuiltin_x86_helper_sha256rnds2,
                                          BUSTER_ARRAY_LENGTH(cbuiltin_x86_helper_sha256rnds2), BUSTER_X86_FEATURE(SHA), "x86 builtins whose instructions the target hasn't", 0, false,
                                          0x07 | BUSTER_CBUILTIN_X86_VECTOR_RESULT)
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESENC128KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesenc128kl_u8", 3, true, cbuiltin_x86_helper_aesenc128kl,
                                                                            BUSTER_X86_EXTENSION(KL), 0x02, 1, 1, 3))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESDEC128KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesdec128kl_u8", 3, true, cbuiltin_x86_helper_aesdec128kl,
                                                                            BUSTER_X86_EXTENSION(KL), 0x02, 1, 1, 3))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESENC256KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesenc256kl_u8", 3, true, cbuiltin_x86_helper_aesenc256kl,
                                                                            BUSTER_X86_EXTENSION(KL), 0x02, 1, 1, 3))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESDEC256KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesdec256kl_u8", 3, true, cbuiltin_x86_helper_aesdec256kl,
                                                                            BUSTER_X86_EXTENSION(KL), 0x02, 1, 1, 3))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESENCWIDE128KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesencwide128kl_u8", 3, true, cbuiltin_x86_helper_aesencwide128kl,
                                                                                BUSTER_X86_EXTENSION(KL) | BUSTER_X86_EXTENSION(WIDEKL), 0, 1, 9, 18))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESDECWIDE128KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesdecwide128kl_u8", 3, true, cbuiltin_x86_helper_aesdecwide128kl,
                                                                                BUSTER_X86_EXTENSION(KL) | BUSTER_X86_EXTENSION(WIDEKL), 0, 1, 9, 18))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESENCWIDE256KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesencwide256kl_u8", 3, true, cbuiltin_x86_helper_aesencwide256kl,
                                                                                BUSTER_X86_EXTENSION(KL) | BUSTER_X86_EXTENSION(WIDEKL), 0, 1, 9, 18))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_AESDECWIDE256KL, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_aesdecwide256kl_u8", 3, true, cbuiltin_x86_helper_aesdecwide256kl,
                                                                                BUSTER_X86_EXTENSION(KL) | BUSTER_X86_EXTENSION(WIDEKL), 0, 1, 9, 18))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ENCODEKEY128, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_encodekey128_u32", 3, true, cbuiltin_x86_helper_encodekey128,
                                                                             BUSTER_X86_EXTENSION(KL), 0x02, 0, 0, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_ENCODEKEY256, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_encodekey256_u32", 4, true, cbuiltin_x86_helper_encodekey256,
                                                                             BUSTER_X86_EXTENSION(KL), 0x06, 0, 0, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_LOADIWKEY, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_loadiwkey", 4, false, cbuiltin_x86_helper_loadiwkey, BUSTER_X86_EXTENSION(KL),
                                                                          0x07, 0, 0, 0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_UMWAIT, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_umwait", 3, true, cbuiltin_x86_helper_umwait, BUSTER_X86_EXTENSION(WAITPKG), 0, 0, 0,
                                                                       0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_TPAUSE, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_tpause", 3, true, cbuiltin_x86_helper_tpause, BUSTER_X86_EXTENSION(WAITPKG), 0, 0, 0,
                                                                       0))
    BUSTER_CBUILTIN_X86_LEAF(CBUILTIN_X86_HELPER_XBEGIN, BUSTER_CBUILTIN_X86_EXTENSION_HELPER("__builtin_ia32_xbegin", 0, true, cbuiltin_x86_helper_xbegin, BUSTER_X86_EXTENSION(RTM), 0, 0, 0, 0))
#undef BUSTER_CBUILTIN_X86_EXTENSION_HELPER
#undef BUSTER_CBUILTIN_X86_HELPER
