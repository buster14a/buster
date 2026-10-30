// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared builtin identities and capability metadata for the preprocessor, checker and selectors (#148).
// A fold action is not a source capability: SOURCE requires source checking and the shipped Tier 0 consumer.
// CONSTEXPR additionally requires source constant evaluation. Tier 1 remains an independent, unadvertised capability.
// This file has no SIMD/encoder dependency. Bounded name slices need no terminator or readable padding.
#ifndef BUSTER_C_BUILTIN_CATALOG_INCLUDED
#define BUSTER_C_BUILTIN_CATALOG_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define BUSTER_CBUILTIN_CAP_FOLD 0x01u
#define BUSTER_CBUILTIN_CAP_SOURCE 0x02u
#define BUSTER_CBUILTIN_CAP_CONSTEXPR 0x04u
#define BUSTER_CBUILTIN_CAP_CHECKER 0x08u
#define BUSTER_CBUILTIN_CAP_TIER0 0x10u
#define BUSTER_CBUILTIN_CAP_TIER1 0x20u
#define BUSTER_CBUILTIN_MAX_NAME 48u

// Source C emits through Tier 0; the experimental Tier 1 action tests are not a source-availability gate (#806).
#define BUSTER_CBUILTIN_SOURCE (BUSTER_CBUILTIN_CAP_SOURCE | BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0)
#define BUSTER_CBUILTIN_CONSTANT (BUSTER_CBUILTIN_SOURCE | BUSTER_CBUILTIN_CAP_CONSTEXPR)

#define BUSTER_CBUILTIN_NAMES(X) \
    X(BSWAP16, "__builtin_bswap16") \
    X(BSWAP32, "__builtin_bswap32") \
    X(BSWAP64, "__builtin_bswap64") \
    X(CLZ, "__builtin_clz") \
    X(CLZL, "__builtin_clzl") \
    X(CLZLL, "__builtin_clzll") \
    X(CTZ, "__builtin_ctz") \
    X(CTZL, "__builtin_ctzl") \
    X(CTZLL, "__builtin_ctzll") \
    X(POPCOUNT, "__builtin_popcount") \
    X(POPCOUNTL, "__builtin_popcountl") \
    X(POPCOUNTLL, "__builtin_popcountll") \
    X(PARITY, "__builtin_parity") \
    X(PARITYL, "__builtin_parityl") \
    X(PARITYLL, "__builtin_parityll") \
    X(FFS, "__builtin_ffs") \
    X(FFSL, "__builtin_ffsl") \
    X(FFSLL, "__builtin_ffsll") \
    X(ADD_OVERFLOW, "__builtin_add_overflow") \
    X(SUB_OVERFLOW, "__builtin_sub_overflow") \
    X(MUL_OVERFLOW, "__builtin_mul_overflow")

// Exact source spellings and audited source capabilities. A zero entry is recognized but has no complete native
// source path. In particular vector bit reversal remains refused. The scoped families (__scoped_atomic_*,
// __hip_atomic_* and __opencl_atomic_*) are their base builtins' operations with a scope (c_check_builtin_atomic_c11.c).
// Availability does not promise every operand type, ISA or ABI: the typed checker and lowering retain their explicit
// restrictions.
// Family ranges leave room for later additions without renumbering any published identity.
#define BUSTER_CBUILTIN_SPECIAL_NAMES(X) \
    X(VA_START, 0x100u, "__builtin_va_start", BUSTER_CBUILTIN_SOURCE) \
    X(C23_VA_START, 0x101u, "__builtin_c23_va_start", BUSTER_CBUILTIN_SOURCE) \
    X(VA_ARG, 0x102u, "__builtin_va_arg", BUSTER_CBUILTIN_SOURCE) \
    X(VA_COPY, 0x103u, "__builtin_va_copy", BUSTER_CBUILTIN_SOURCE) \
    X(VA_END, 0x104u, "__builtin_va_end", BUSTER_CBUILTIN_SOURCE) \
    X(OFFSETOF, 0x110u, "__builtin_offsetof", BUSTER_CBUILTIN_CONSTANT) \
    X(EXPECT, 0x120u, "__builtin_expect", BUSTER_CBUILTIN_CONSTANT) \
    X(UNREACHABLE, 0x121u, "__builtin_unreachable", BUSTER_CBUILTIN_SOURCE) \
    X(TRAP, 0x122u, "__builtin_trap", BUSTER_CBUILTIN_SOURCE) \
    X(ASSUME_ALIGNED, 0x123u, "__builtin_assume_aligned", BUSTER_CBUILTIN_SOURCE) \
    X(PREFETCH, 0x124u, "__builtin_prefetch", BUSTER_CBUILTIN_SOURCE) \
    X(CONSTANT_P, 0x130u, "__builtin_constant_p", BUSTER_CBUILTIN_CONSTANT) \
    X(TYPES_COMPATIBLE_P, 0x131u, "__builtin_types_compatible_p", BUSTER_CBUILTIN_CONSTANT) \
    X(CHOOSE_EXPR, 0x132u, "__builtin_choose_expr", BUSTER_CBUILTIN_CONSTANT) \
    X(OBJECT_SIZE, 0x133u, "__builtin_object_size", BUSTER_CBUILTIN_SOURCE) \
    X(DYNAMIC_OBJECT_SIZE, 0x134u, "__builtin_dynamic_object_size", BUSTER_CBUILTIN_SOURCE) \
    X(HUGE_VAL, 0x140u, "__builtin_huge_val", BUSTER_CBUILTIN_SOURCE) \
    X(HUGE_VALF, 0x141u, "__builtin_huge_valf", BUSTER_CBUILTIN_SOURCE) \
    X(HUGE_VALL, 0x142u, "__builtin_huge_vall", BUSTER_CBUILTIN_SOURCE) \
    X(INF, 0x143u, "__builtin_inf", BUSTER_CBUILTIN_SOURCE) \
    X(INFF, 0x144u, "__builtin_inff", BUSTER_CBUILTIN_SOURCE) \
    X(INFL, 0x145u, "__builtin_infl", BUSTER_CBUILTIN_SOURCE) \
    X(NAN, 0x146u, "__builtin_nan", BUSTER_CBUILTIN_SOURCE) \
    X(NANF, 0x147u, "__builtin_nanf", BUSTER_CBUILTIN_SOURCE) \
    X(NANL, 0x148u, "__builtin_nanl", BUSTER_CBUILTIN_SOURCE) \
    X(ISNAN, 0x149u, "__builtin_isnan", BUSTER_CBUILTIN_SOURCE) \
    X(ISINF, 0x14au, "__builtin_isinf", BUSTER_CBUILTIN_SOURCE) \
    X(ISFINITE, 0x14bu, "__builtin_isfinite", BUSTER_CBUILTIN_SOURCE) \
    X(SIGNBIT, 0x14cu, "__builtin_signbit", BUSTER_CBUILTIN_SOURCE) \
    X(FPCLASSIFY, 0x14du, "__builtin_fpclassify", BUSTER_CBUILTIN_SOURCE) \
    X(ISINF_SIGN, 0x14eu, "__builtin_isinf_sign", BUSTER_CBUILTIN_SOURCE) \
    X(FABS, 0x14fu, "__builtin_fabs", BUSTER_CBUILTIN_SOURCE) \
    X(FABSF, 0x150u, "__builtin_fabsf", BUSTER_CBUILTIN_SOURCE) \
    X(FABSL, 0x151u, "__builtin_fabsl", BUSTER_CBUILTIN_SOURCE) \
    X(FLT_ROUNDS, 0x152u, "__builtin_flt_rounds", BUSTER_CBUILTIN_SOURCE) \
    X(CREAL, 0x153u, "__builtin_creal", BUSTER_CBUILTIN_SOURCE) \
    X(CREALF, 0x154u, "__builtin_crealf", BUSTER_CBUILTIN_SOURCE) \
    X(CREALL, 0x155u, "__builtin_creall", BUSTER_CBUILTIN_SOURCE) \
    X(CIMAG, 0x156u, "__builtin_cimag", BUSTER_CBUILTIN_SOURCE) \
    X(CIMAGF, 0x157u, "__builtin_cimagf", BUSTER_CBUILTIN_SOURCE) \
    X(CIMAGL, 0x158u, "__builtin_cimagl", BUSTER_CBUILTIN_SOURCE) \
    X(ISGREATER, 0x159u, "__builtin_isgreater", BUSTER_CBUILTIN_SOURCE) \
    X(ISGREATEREQUAL, 0x15au, "__builtin_isgreaterequal", BUSTER_CBUILTIN_SOURCE) \
    X(ISLESS, 0x15bu, "__builtin_isless", BUSTER_CBUILTIN_SOURCE) \
    X(ISLESSEQUAL, 0x15cu, "__builtin_islessequal", BUSTER_CBUILTIN_SOURCE) \
    X(ISLESSGREATER, 0x15du, "__builtin_islessgreater", BUSTER_CBUILTIN_SOURCE) \
    X(ISUNORDERED, 0x15eu, "__builtin_isunordered", BUSTER_CBUILTIN_SOURCE) \
    X(SQRT, 0x200u, "__builtin_sqrt", BUSTER_CBUILTIN_SOURCE) \
    X(SQRTF, 0x201u, "__builtin_sqrtf", BUSTER_CBUILTIN_SOURCE) \
    X(COPYSIGN, 0x202u, "__builtin_copysign", BUSTER_CBUILTIN_SOURCE) \
    X(COPYSIGNF, 0x203u, "__builtin_copysignf", BUSTER_CBUILTIN_SOURCE) \
    X(CEIL, 0x204u, "__builtin_ceil", BUSTER_CBUILTIN_SOURCE) \
    X(CEILF, 0x205u, "__builtin_ceilf", BUSTER_CBUILTIN_SOURCE) \
    X(FLOOR, 0x206u, "__builtin_floor", BUSTER_CBUILTIN_SOURCE) \
    X(FLOORF, 0x207u, "__builtin_floorf", BUSTER_CBUILTIN_SOURCE) \
    X(TRUNC, 0x208u, "__builtin_trunc", BUSTER_CBUILTIN_SOURCE) \
    X(TRUNCF, 0x209u, "__builtin_truncf", BUSTER_CBUILTIN_SOURCE) \
    X(NEARBYINT, 0x20au, "__builtin_nearbyint", BUSTER_CBUILTIN_SOURCE) \
    X(NEARBYINTF, 0x20bu, "__builtin_nearbyintf", BUSTER_CBUILTIN_SOURCE) \
    X(RINT, 0x20cu, "__builtin_rint", BUSTER_CBUILTIN_SOURCE) \
    X(RINTF, 0x20du, "__builtin_rintf", BUSTER_CBUILTIN_SOURCE) \
    X(MEMCPY, 0x160u, "__builtin_memcpy", BUSTER_CBUILTIN_SOURCE) \
    X(MEMSET, 0x161u, "__builtin_memset", BUSTER_CBUILTIN_SOURCE) \
    X(MEMMOVE, 0x162u, "__builtin_memmove", BUSTER_CBUILTIN_SOURCE) \
    X(MEMCMP, 0x163u, "__builtin_memcmp", BUSTER_CBUILTIN_SOURCE) \
    X(STRLEN, 0x164u, "__builtin_strlen", BUSTER_CBUILTIN_SOURCE) \
    X(ALLOCA, 0x165u, "__builtin_alloca", BUSTER_CBUILTIN_SOURCE) \
    X(UMULL_OVERFLOW, 0x170u, "__builtin_umull_overflow", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_LOAD, 0x180u, "__atomic_load", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_LOAD_N, 0x181u, "__atomic_load_n", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_STORE, 0x182u, "__atomic_store", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_STORE_N, 0x183u, "__atomic_store_n", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_EXCHANGE, 0x184u, "__atomic_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_EXCHANGE_N, 0x185u, "__atomic_exchange_n", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_COMPARE_EXCHANGE, 0x186u, "__atomic_compare_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_COMPARE_EXCHANGE_N, 0x187u, "__atomic_compare_exchange_n", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_ADD, 0x188u, "__atomic_fetch_add", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_SUB, 0x189u, "__atomic_fetch_sub", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_AND, 0x18au, "__atomic_fetch_and", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_XOR, 0x18bu, "__atomic_fetch_xor", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_OR, 0x18cu, "__atomic_fetch_or", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_NAND, 0x18du, "__atomic_fetch_nand", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_ADD_FETCH, 0x18eu, "__atomic_add_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_SUB_FETCH, 0x18fu, "__atomic_sub_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_AND_FETCH, 0x190u, "__atomic_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_XOR_FETCH, 0x191u, "__atomic_xor_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_OR_FETCH, 0x192u, "__atomic_or_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_NAND_FETCH, 0x193u, "__atomic_nand_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_TEST_AND_SET, 0x194u, "__atomic_test_and_set", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_CLEAR, 0x195u, "__atomic_clear", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_THREAD_FENCE, 0x196u, "__atomic_thread_fence", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_SIGNAL_FENCE, 0x197u, "__atomic_signal_fence", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_ALWAYS_LOCK_FREE, 0x198u, "__atomic_always_lock_free", BUSTER_CBUILTIN_CONSTANT) \
    X(ATOMIC_IS_LOCK_FREE, 0x199u, "__atomic_is_lock_free", BUSTER_CBUILTIN_CONSTANT) \
    X(ATOMIC_FETCH_MIN, 0x19au, "__atomic_fetch_min", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_MAX, 0x19bu, "__atomic_fetch_max", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_MIN_FETCH, 0x19cu, "__atomic_min_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_MAX_FETCH, 0x19du, "__atomic_max_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_UINC, 0x19eu, "__atomic_fetch_uinc", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_UDEC, 0x19fu, "__atomic_fetch_udec", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_INIT, 0x1a0u, "__c11_atomic_init", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_LOAD, 0x1a1u, "__c11_atomic_load", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_STORE, 0x1a2u, "__c11_atomic_store", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_EXCHANGE, 0x1a3u, "__c11_atomic_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_COMPARE_EXCHANGE_STRONG, 0x1a4u, "__c11_atomic_compare_exchange_strong", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_COMPARE_EXCHANGE_WEAK, 0x1a5u, "__c11_atomic_compare_exchange_weak", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_ADD, 0x1a6u, "__c11_atomic_fetch_add", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_SUB, 0x1a7u, "__c11_atomic_fetch_sub", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_AND, 0x1a8u, "__c11_atomic_fetch_and", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_OR, 0x1a9u, "__c11_atomic_fetch_or", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_XOR, 0x1aau, "__c11_atomic_fetch_xor", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_NAND, 0x1abu, "__c11_atomic_fetch_nand", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_MIN, 0x1acu, "__c11_atomic_fetch_min", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_FETCH_MAX, 0x1adu, "__c11_atomic_fetch_max", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_THREAD_FENCE, 0x1aeu, "__c11_atomic_thread_fence", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_SIGNAL_FENCE, 0x1afu, "__c11_atomic_signal_fence", BUSTER_CBUILTIN_SOURCE) \
    X(C11_ATOMIC_IS_LOCK_FREE, 0x1b0u, "__c11_atomic_is_lock_free", BUSTER_CBUILTIN_CONSTANT) \
    X(ATOMIC_FETCH_FMAXIMUM, 0x1b8u, "__atomic_fetch_fmaximum", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_FMAXIMUM_NUM, 0x1b9u, "__atomic_fetch_fmaximum_num", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_FMINIMUM, 0x1bau, "__atomic_fetch_fminimum", BUSTER_CBUILTIN_SOURCE) \
    X(ATOMIC_FETCH_FMINIMUM_NUM, 0x1bbu, "__atomic_fetch_fminimum_num", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_ADD, 0x1c0u, "__sync_fetch_and_add", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_SUB, 0x1c1u, "__sync_fetch_and_sub", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_OR, 0x1c2u, "__sync_fetch_and_or", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_AND, 0x1c3u, "__sync_fetch_and_and", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_XOR, 0x1c4u, "__sync_fetch_and_xor", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_NAND, 0x1c5u, "__sync_fetch_and_nand", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_ADD_AND_FETCH, 0x1c6u, "__sync_add_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_SUB_AND_FETCH, 0x1c7u, "__sync_sub_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_OR_AND_FETCH, 0x1c8u, "__sync_or_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_AND_AND_FETCH, 0x1c9u, "__sync_and_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_XOR_AND_FETCH, 0x1cau, "__sync_xor_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_NAND_AND_FETCH, 0x1cbu, "__sync_nand_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_BOOL_COMPARE_AND_SWAP, 0x1ccu, "__sync_bool_compare_and_swap", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_VAL_COMPARE_AND_SWAP, 0x1cdu, "__sync_val_compare_and_swap", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_SYNCHRONIZE, 0x1ceu, "__sync_synchronize", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_LOCK_TEST_AND_SET, 0x1cfu, "__sync_lock_test_and_set", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_LOCK_RELEASE, 0x1d0u, "__sync_lock_release", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_SWAP, 0x1d1u, "__sync_swap", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_MIN, 0x1d2u, "__sync_fetch_and_min", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_MAX, 0x1d3u, "__sync_fetch_and_max", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_UMIN, 0x1d4u, "__sync_fetch_and_umin", BUSTER_CBUILTIN_SOURCE) \
    X(SYNC_FETCH_AND_UMAX, 0x1d5u, "__sync_fetch_and_umax", BUSTER_CBUILTIN_SOURCE) \
    X(FRAME_ADDRESS, 0x1f0u, "__builtin_frame_address", BUSTER_CBUILTIN_SOURCE) \
    X(RETURN_ADDRESS, 0x1f1u, "__builtin_return_address", BUSTER_CBUILTIN_SOURCE) \
    X(EXTRACT_RETURN_ADDR, 0x1f2u, "__builtin_extract_return_addr", BUSTER_CBUILTIN_SOURCE) \
    X(FROB_RETURN_ADDR, 0x1f3u, "__builtin_frob_return_addr", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_ABS, 0x300u, "__builtin_elementwise_abs", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_ADD_SAT, 0x301u, "__builtin_elementwise_add_sat", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_BITREVERSE, 0x302u, "__builtin_elementwise_bitreverse", 0) \
    X(ELEMENTWISE_CLZG, 0x303u, "__builtin_elementwise_clzg", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_FMA, 0x304u, "__builtin_elementwise_fma", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_FSHL, 0x305u, "__builtin_elementwise_fshl", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_FSHR, 0x306u, "__builtin_elementwise_fshr", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_MAX, 0x307u, "__builtin_elementwise_max", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_MIN, 0x308u, "__builtin_elementwise_min", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_POPCOUNT, 0x309u, "__builtin_elementwise_popcount", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_SQRT, 0x30au, "__builtin_elementwise_sqrt", BUSTER_CBUILTIN_SOURCE) \
    X(ELEMENTWISE_SUB_SAT, 0x30bu, "__builtin_elementwise_sub_sat", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_ADD, 0x30cu, "__builtin_reduce_add", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_AND, 0x30du, "__builtin_reduce_and", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_MAX, 0x30eu, "__builtin_reduce_max", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_MIN, 0x30fu, "__builtin_reduce_min", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_MUL, 0x310u, "__builtin_reduce_mul", BUSTER_CBUILTIN_SOURCE) \
    X(REDUCE_OR, 0x311u, "__builtin_reduce_or", BUSTER_CBUILTIN_SOURCE) \
    X(SHUFFLEVECTOR, 0x312u, "__builtin_shufflevector", BUSTER_CBUILTIN_SOURCE) \
    X(CONVERTVECTOR, 0x313u, "__builtin_convertvector", BUSTER_CBUILTIN_SOURCE) \
    X(NONDETERMINISTIC_VALUE, 0x314u, "__builtin_nondeterministic_value", BUSTER_CBUILTIN_SOURCE) \
    X(NONTEMPORAL_LOAD, 0x315u, "__builtin_nontemporal_load", BUSTER_CBUILTIN_SOURCE) \
    X(NONTEMPORAL_STORE, 0x316u, "__builtin_nontemporal_store", BUSTER_CBUILTIN_SOURCE) \
    X(BIT_CAST, 0x317u, "__builtin_bit_cast", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_ADD_FETCH, 0x400u, "__scoped_atomic_add_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_AND_FETCH, 0x401u, "__scoped_atomic_and_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_COMPARE_EXCHANGE, 0x402u, "__scoped_atomic_compare_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_COMPARE_EXCHANGE_N, 0x403u, "__scoped_atomic_compare_exchange_n", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_EXCHANGE, 0x404u, "__scoped_atomic_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_EXCHANGE_N, 0x405u, "__scoped_atomic_exchange_n", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_ADD, 0x406u, "__scoped_atomic_fetch_add", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_AND, 0x407u, "__scoped_atomic_fetch_and", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_FMAXIMUM, 0x408u, "__scoped_atomic_fetch_fmaximum", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_FMAXIMUM_NUM, 0x409u, "__scoped_atomic_fetch_fmaximum_num", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_FMINIMUM, 0x40au, "__scoped_atomic_fetch_fminimum", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_FMINIMUM_NUM, 0x40bu, "__scoped_atomic_fetch_fminimum_num", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_MAX, 0x40cu, "__scoped_atomic_fetch_max", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_MIN, 0x40du, "__scoped_atomic_fetch_min", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_NAND, 0x40eu, "__scoped_atomic_fetch_nand", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_OR, 0x40fu, "__scoped_atomic_fetch_or", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_SUB, 0x410u, "__scoped_atomic_fetch_sub", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_UDEC, 0x411u, "__scoped_atomic_fetch_udec", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_UINC, 0x412u, "__scoped_atomic_fetch_uinc", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_FETCH_XOR, 0x413u, "__scoped_atomic_fetch_xor", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_LOAD, 0x414u, "__scoped_atomic_load", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_LOAD_N, 0x415u, "__scoped_atomic_load_n", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_MAX_FETCH, 0x416u, "__scoped_atomic_max_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_MIN_FETCH, 0x417u, "__scoped_atomic_min_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_NAND_FETCH, 0x418u, "__scoped_atomic_nand_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_OR_FETCH, 0x419u, "__scoped_atomic_or_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_STORE, 0x41au, "__scoped_atomic_store", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_STORE_N, 0x41bu, "__scoped_atomic_store_n", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_SUB_FETCH, 0x41cu, "__scoped_atomic_sub_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_THREAD_FENCE, 0x41du, "__scoped_atomic_thread_fence", BUSTER_CBUILTIN_SOURCE) \
    X(SCOPED_ATOMIC_XOR_FETCH, 0x41eu, "__scoped_atomic_xor_fetch", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_COMPARE_EXCHANGE_STRONG, 0x420u, "__hip_atomic_compare_exchange_strong", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_COMPARE_EXCHANGE_WEAK, 0x421u, "__hip_atomic_compare_exchange_weak", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_EXCHANGE, 0x422u, "__hip_atomic_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_ADD, 0x423u, "__hip_atomic_fetch_add", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_AND, 0x424u, "__hip_atomic_fetch_and", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_MAX, 0x425u, "__hip_atomic_fetch_max", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_MIN, 0x426u, "__hip_atomic_fetch_min", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_OR, 0x427u, "__hip_atomic_fetch_or", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_SUB, 0x428u, "__hip_atomic_fetch_sub", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_FETCH_XOR, 0x429u, "__hip_atomic_fetch_xor", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_LOAD, 0x42au, "__hip_atomic_load", BUSTER_CBUILTIN_SOURCE) \
    X(HIP_ATOMIC_STORE, 0x42bu, "__hip_atomic_store", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_COMPARE_EXCHANGE_STRONG, 0x430u, "__opencl_atomic_compare_exchange_strong", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_COMPARE_EXCHANGE_WEAK, 0x431u, "__opencl_atomic_compare_exchange_weak", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_EXCHANGE, 0x432u, "__opencl_atomic_exchange", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_ADD, 0x433u, "__opencl_atomic_fetch_add", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_AND, 0x434u, "__opencl_atomic_fetch_and", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_MAX, 0x435u, "__opencl_atomic_fetch_max", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_MIN, 0x436u, "__opencl_atomic_fetch_min", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_OR, 0x437u, "__opencl_atomic_fetch_or", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_SUB, 0x438u, "__opencl_atomic_fetch_sub", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_FETCH_XOR, 0x439u, "__opencl_atomic_fetch_xor", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_INIT, 0x43au, "__opencl_atomic_init", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_LOAD, 0x43bu, "__opencl_atomic_load", BUSTER_CBUILTIN_SOURCE) \
    X(OPENCL_ATOMIC_STORE, 0x43cu, "__opencl_atomic_store", BUSTER_CBUILTIN_SOURCE)

typedef enum CBuiltinId
{
    CBUILTIN_NONE,
#define BUSTER_CBUILTIN_ENUM(id, name) CBUILTIN_##id,
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_ENUM)
#undef BUSTER_CBUILTIN_ENUM
    // Integer action sentinel: existing action loops and vector descriptors stop here.
    CBUILTIN_COUNT,
#define BUSTER_CBUILTIN_SPECIAL_ENUM(id, value, name, capabilities) CBUILTIN_##id = value,
    BUSTER_CBUILTIN_SPECIAL_NAMES(BUSTER_CBUILTIN_SPECIAL_ENUM)
#undef BUSTER_CBUILTIN_SPECIAL_ENUM
} CBuiltinId;

typedef struct CBuiltinName CBuiltinName;
struct CBuiltinName
{
    char bytes[32];
    uint32_t length;
};

static const CBuiltinName cbuiltin_names[CBUILTIN_COUNT] __attribute__((unused)) =
{
    { "", 0 },
#define BUSTER_CBUILTIN_NAME(id, name) { name, sizeof(name) - 1 },
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_NAME)
#undef BUSTER_CBUILTIN_NAME
};

typedef struct CBuiltinCatalogEntry CBuiltinCatalogEntry;
struct CBuiltinCatalogEntry
{
    CBuiltinId id;
    const char* name;
    uint32_t length;
    uint8_t capabilities;
};

// A recognized identity, checked action or standalone native fixture is insufficient for SOURCE. The semantic
// module retains arity/type-argument rules; no guessed common function signature is attached to these identities.
static const CBuiltinCatalogEntry cbuiltin_catalog[] =
{
#define BUSTER_CBUILTIN_INTEGER_ENTRY(id, name) { CBUILTIN_##id, name, sizeof(name) - 1, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_SOURCE | \
    (CBUILTIN_##id <= CBUILTIN_FFSLL ? BUSTER_CBUILTIN_CAP_CONSTEXPR : 0) },
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_INTEGER_ENTRY)
#undef BUSTER_CBUILTIN_INTEGER_ENTRY
#define BUSTER_CBUILTIN_SPECIAL_ENTRY(id, value, name, capabilities) { CBUILTIN_##id, name, sizeof(name) - 1, capabilities },
    BUSTER_CBUILTIN_SPECIAL_NAMES(BUSTER_CBUILTIN_SPECIAL_ENTRY)
#undef BUSTER_CBUILTIN_SPECIAL_ENTRY
};

_Static_assert(CBUILTIN_BSWAP16 == 1 && CBUILTIN_FFSLL == 18 && CBUILTIN_ADD_OVERFLOW == 19 && CBUILTIN_MUL_OVERFLOW == 21 && CBUILTIN_COUNT == 22,
               "integer builtin action identities remain stable");
_Static_assert(CBUILTIN_VA_START == 0x100 && CBUILTIN_VA_END == 0x104, "special builtin identities remain outside integer actions");

// The catalog's entry of an exact spelling, or null. A name's bytes are inspected only after its length matches.
static __attribute__((unused)) const CBuiltinCatalogEntry* cbuiltin_entry(const char* name, uint32_t length)
{
    const CBuiltinCatalogEntry* result = NULL;
    const uint32_t count = sizeof(cbuiltin_catalog) / sizeof(cbuiltin_catalog[0]);
    for (uint32_t i = 0; i < count && result == NULL; i += 1)
    {
        if (length == cbuiltin_catalog[i].length && memcmp(name, cbuiltin_catalog[i].name, length) == 0)
        {
            result = cbuiltin_catalog + i;
        }
    }
    return result;
}

// clang's sized legacy builtins, __sync_fetch_and_add_4 and the others of 1, 2, 4, 8 and 16 bytes (#148): Sema rewrites a
// call of one by its object's size alone, whatever the suffix says (BuiltinAtomicOverloaded: __sync_fetch_and_add_1 of a
// long is an 8-byte addition), so each is its family's builtin, which the name less its suffix spells. The family's
// entry, for the legacy families whose rows in clang's table have sized forms (all but __sync_synchronize and the minimum
// and maximum), or null for any other name. Only a name that ends in a suffix and starts with __sync_ is looked up again.
static __attribute__((unused)) const CBuiltinCatalogEntry* cbuiltin_sized_entry(const char* name, uint32_t length)
{
    const char last = length > 9 ? name[length - 1] : 0;
    const uint32_t suffix = length > 9 && name[length - 2] == '_' && (last == '1' || last == '2' || last == '4' || last == '8') ? 2 :
                            length > 10 && name[length - 3] == '_' && name[length - 2] == '1' && last == '6'                  ? 3 :
                                                                                                                                  0;
    const CBuiltinCatalogEntry* entry = suffix != 0 && memcmp(name, "__sync_", 7) == 0 ? cbuiltin_entry(name, length - suffix) : NULL;
    const bool sized = entry != NULL && entry->id >= CBUILTIN_SYNC_FETCH_AND_ADD && entry->id <= CBUILTIN_SYNC_SWAP && entry->id != CBUILTIN_SYNC_SYNCHRONIZE;
    return sized ? entry : NULL;
}

// Binding and feature queries resolve bounded exact spellings. The integer action layer's SIMD/scalar lookups
// deliberately retain their old integer-only contract. Every catalog name starts with "__" (test-preprocessor checks
// it), so a name that doesn't is no builtin's, which the checker asks of each distinct identifier of a unit (#529). A
// sized legacy name resolves to its family (cbuiltin_sized_entry).
static __attribute__((unused)) CBuiltinId cbuiltin_resolve(const char* name, uint32_t length)
{
    CBuiltinId result = CBUILTIN_NONE;
    if (name != 0 && length >= 2 && length < BUSTER_CBUILTIN_MAX_NAME && name[0] == '_' && name[1] == '_')
    {
        const CBuiltinCatalogEntry* entry = cbuiltin_entry(name, length);
        entry = entry != NULL ? entry : cbuiltin_sized_entry(name, length);
        result = entry != NULL ? entry->id : CBUILTIN_NONE;
    }
    return result;
}

static __attribute__((unused)) bool cbuiltin_has_capability(const uint8_t* name, uint32_t length, uint32_t capability)
{
    bool result = false;
    uint32_t required = capability;
    const uint32_t allowed = BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_CAP_SOURCE | BUSTER_CBUILTIN_CAP_CONSTEXPR |
                             BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0 | BUSTER_CBUILTIN_CAP_TIER1;
    if ((required & BUSTER_CBUILTIN_CAP_CONSTEXPR) != 0)
    {
        required |= BUSTER_CBUILTIN_CAP_SOURCE;
    }
    if ((required & BUSTER_CBUILTIN_CAP_SOURCE) != 0)
    {
        required |= BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0;
    }
    if (name != 0 && required != 0 && (required & ~allowed) == 0 && length != 0 && length < BUSTER_CBUILTIN_MAX_NAME)
    {
        // A sized legacy name has its family's capabilities (cbuiltin_sized_entry).
        const CBuiltinCatalogEntry* entry = cbuiltin_entry((const char*)name, length);
        entry = entry != NULL ? entry : cbuiltin_sized_entry((const char*)name, length);
        result = entry != NULL && (entry->capabilities & required) == required;
    }
    return result;
}

// The language modes' token of clang 23.1.1's table for a catalog name, which the preprocessor's __has_builtin reads
// (#148): C23_LANG for __builtin_c23_va_start, a builtin in C23 alone, and ALL_LANGUAGES for every other name.
// test-preprocessor checks each name's against the table.
static __attribute__((unused)) const char* cbuiltin_languages(const uint8_t* name, uint32_t length)
{
    const char* result = cbuiltin_resolve((const char*)name, length) == CBUILTIN_C23_VA_START ? "C23_LANG" : "ALL_LANGUAGES";
    return result;
}

#endif
