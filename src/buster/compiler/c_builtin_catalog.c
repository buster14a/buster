// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared builtin identities and capability metadata for the preprocessor, checker and selectors (#148).
// A fold action is not a source capability: SOURCE requires checking and both required runtime consumers, and
// CONSTEXPR additionally requires source constant evaluation. Keep these flags separate when integration lands.
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

// Exact source spellings only. These stable identities do not advertise implementation or invent a signature.
// Family ranges leave room for later additions without renumbering any published identity.
#define BUSTER_CBUILTIN_SPECIAL_NAMES(X) \
    X(VA_START, 0x100u, "__builtin_va_start") \
    X(C23_VA_START, 0x101u, "__builtin_c23_va_start") \
    X(VA_ARG, 0x102u, "__builtin_va_arg") \
    X(VA_COPY, 0x103u, "__builtin_va_copy") \
    X(VA_END, 0x104u, "__builtin_va_end") \
    X(OFFSETOF, 0x110u, "__builtin_offsetof") \
    X(EXPECT, 0x120u, "__builtin_expect") \
    X(UNREACHABLE, 0x121u, "__builtin_unreachable") \
    X(TRAP, 0x122u, "__builtin_trap") \
    X(ASSUME_ALIGNED, 0x123u, "__builtin_assume_aligned") \
    X(PREFETCH, 0x124u, "__builtin_prefetch") \
    X(CONSTANT_P, 0x130u, "__builtin_constant_p") \
    X(TYPES_COMPATIBLE_P, 0x131u, "__builtin_types_compatible_p") \
    X(CHOOSE_EXPR, 0x132u, "__builtin_choose_expr") \
    X(OBJECT_SIZE, 0x133u, "__builtin_object_size") \
    X(HUGE_VAL, 0x140u, "__builtin_huge_val") \
    X(HUGE_VALF, 0x141u, "__builtin_huge_valf") \
    X(HUGE_VALL, 0x142u, "__builtin_huge_vall") \
    X(INF, 0x143u, "__builtin_inf") \
    X(INFF, 0x144u, "__builtin_inff") \
    X(INFL, 0x145u, "__builtin_infl") \
    X(NAN, 0x146u, "__builtin_nan") \
    X(NANF, 0x147u, "__builtin_nanf") \
    X(NANL, 0x148u, "__builtin_nanl") \
    X(ISNAN, 0x149u, "__builtin_isnan") \
    X(ISINF, 0x14au, "__builtin_isinf") \
    X(ISFINITE, 0x14bu, "__builtin_isfinite") \
    X(SIGNBIT, 0x14cu, "__builtin_signbit") \
    X(FPCLASSIFY, 0x14du, "__builtin_fpclassify") \
    X(ISINF_SIGN, 0x14eu, "__builtin_isinf_sign") \
    X(FABS, 0x14fu, "__builtin_fabs") \
    X(FABSF, 0x150u, "__builtin_fabsf") \
    X(FABSL, 0x151u, "__builtin_fabsl") \
    X(FLT_ROUNDS, 0x152u, "__builtin_flt_rounds") \
    X(CREAL, 0x153u, "__builtin_creal") \
    X(CREALF, 0x154u, "__builtin_crealf") \
    X(CREALL, 0x155u, "__builtin_creall") \
    X(CIMAG, 0x156u, "__builtin_cimag") \
    X(CIMAGF, 0x157u, "__builtin_cimagf") \
    X(CIMAGL, 0x158u, "__builtin_cimagl") \
    X(ISGREATER, 0x159u, "__builtin_isgreater") \
    X(ISGREATEREQUAL, 0x15au, "__builtin_isgreaterequal") \
    X(ISLESS, 0x15bu, "__builtin_isless") \
    X(ISLESSEQUAL, 0x15cu, "__builtin_islessequal") \
    X(ISLESSGREATER, 0x15du, "__builtin_islessgreater") \
    X(ISUNORDERED, 0x15eu, "__builtin_isunordered") \
    X(SQRT, 0x200u, "__builtin_sqrt") \
    X(SQRTF, 0x201u, "__builtin_sqrtf") \
    X(COPYSIGN, 0x202u, "__builtin_copysign") \
    X(COPYSIGNF, 0x203u, "__builtin_copysignf") \
    X(CEIL, 0x204u, "__builtin_ceil") \
    X(CEILF, 0x205u, "__builtin_ceilf") \
    X(FLOOR, 0x206u, "__builtin_floor") \
    X(FLOORF, 0x207u, "__builtin_floorf") \
    X(TRUNC, 0x208u, "__builtin_trunc") \
    X(TRUNCF, 0x209u, "__builtin_truncf") \
    X(NEARBYINT, 0x20au, "__builtin_nearbyint") \
    X(NEARBYINTF, 0x20bu, "__builtin_nearbyintf") \
    X(RINT, 0x20cu, "__builtin_rint") \
    X(RINTF, 0x20du, "__builtin_rintf") \
    X(MEMCPY, 0x160u, "__builtin_memcpy") \
    X(MEMSET, 0x161u, "__builtin_memset") \
    X(MEMMOVE, 0x162u, "__builtin_memmove") \
    X(MEMCMP, 0x163u, "__builtin_memcmp") \
    X(STRLEN, 0x164u, "__builtin_strlen") \
    X(ALLOCA, 0x165u, "__builtin_alloca") \
    X(UMULL_OVERFLOW, 0x170u, "__builtin_umull_overflow") \
    X(ATOMIC_LOAD, 0x180u, "__atomic_load") \
    X(ATOMIC_LOAD_N, 0x181u, "__atomic_load_n") \
    X(ATOMIC_STORE, 0x182u, "__atomic_store") \
    X(ATOMIC_STORE_N, 0x183u, "__atomic_store_n") \
    X(ATOMIC_EXCHANGE, 0x184u, "__atomic_exchange") \
    X(ATOMIC_EXCHANGE_N, 0x185u, "__atomic_exchange_n") \
    X(ATOMIC_COMPARE_EXCHANGE, 0x186u, "__atomic_compare_exchange") \
    X(ATOMIC_COMPARE_EXCHANGE_N, 0x187u, "__atomic_compare_exchange_n") \
    X(ATOMIC_FETCH_ADD, 0x188u, "__atomic_fetch_add") \
    X(ATOMIC_FETCH_SUB, 0x189u, "__atomic_fetch_sub") \
    X(ATOMIC_FETCH_AND, 0x18au, "__atomic_fetch_and") \
    X(ATOMIC_FETCH_XOR, 0x18bu, "__atomic_fetch_xor") \
    X(ATOMIC_FETCH_OR, 0x18cu, "__atomic_fetch_or") \
    X(ATOMIC_FETCH_NAND, 0x18du, "__atomic_fetch_nand") \
    X(ATOMIC_ADD_FETCH, 0x18eu, "__atomic_add_fetch") \
    X(ATOMIC_SUB_FETCH, 0x18fu, "__atomic_sub_fetch") \
    X(ATOMIC_AND_FETCH, 0x190u, "__atomic_and_fetch") \
    X(ATOMIC_XOR_FETCH, 0x191u, "__atomic_xor_fetch") \
    X(ATOMIC_OR_FETCH, 0x192u, "__atomic_or_fetch") \
    X(ATOMIC_NAND_FETCH, 0x193u, "__atomic_nand_fetch") \
    X(ATOMIC_TEST_AND_SET, 0x194u, "__atomic_test_and_set") \
    X(ATOMIC_CLEAR, 0x195u, "__atomic_clear") \
    X(ATOMIC_THREAD_FENCE, 0x196u, "__atomic_thread_fence") \
    X(ATOMIC_SIGNAL_FENCE, 0x197u, "__atomic_signal_fence") \
    X(ATOMIC_ALWAYS_LOCK_FREE, 0x198u, "__atomic_always_lock_free") \
    X(ATOMIC_IS_LOCK_FREE, 0x199u, "__atomic_is_lock_free") \
    X(ATOMIC_FETCH_MIN, 0x19au, "__atomic_fetch_min") \
    X(ATOMIC_FETCH_MAX, 0x19bu, "__atomic_fetch_max") \
    X(ATOMIC_MIN_FETCH, 0x19cu, "__atomic_min_fetch") \
    X(ATOMIC_MAX_FETCH, 0x19du, "__atomic_max_fetch") \
    X(C11_ATOMIC_INIT, 0x1a0u, "__c11_atomic_init") \
    X(C11_ATOMIC_LOAD, 0x1a1u, "__c11_atomic_load") \
    X(C11_ATOMIC_STORE, 0x1a2u, "__c11_atomic_store") \
    X(C11_ATOMIC_EXCHANGE, 0x1a3u, "__c11_atomic_exchange") \
    X(C11_ATOMIC_COMPARE_EXCHANGE_STRONG, 0x1a4u, "__c11_atomic_compare_exchange_strong") \
    X(C11_ATOMIC_COMPARE_EXCHANGE_WEAK, 0x1a5u, "__c11_atomic_compare_exchange_weak") \
    X(C11_ATOMIC_FETCH_ADD, 0x1a6u, "__c11_atomic_fetch_add") \
    X(C11_ATOMIC_FETCH_SUB, 0x1a7u, "__c11_atomic_fetch_sub") \
    X(C11_ATOMIC_FETCH_AND, 0x1a8u, "__c11_atomic_fetch_and") \
    X(C11_ATOMIC_FETCH_OR, 0x1a9u, "__c11_atomic_fetch_or") \
    X(C11_ATOMIC_FETCH_XOR, 0x1aau, "__c11_atomic_fetch_xor") \
    X(C11_ATOMIC_FETCH_NAND, 0x1abu, "__c11_atomic_fetch_nand") \
    X(C11_ATOMIC_FETCH_MIN, 0x1acu, "__c11_atomic_fetch_min") \
    X(C11_ATOMIC_FETCH_MAX, 0x1adu, "__c11_atomic_fetch_max") \
    X(C11_ATOMIC_THREAD_FENCE, 0x1aeu, "__c11_atomic_thread_fence") \
    X(C11_ATOMIC_SIGNAL_FENCE, 0x1afu, "__c11_atomic_signal_fence") \
    X(C11_ATOMIC_IS_LOCK_FREE, 0x1b0u, "__c11_atomic_is_lock_free") \
    X(SYNC_FETCH_AND_ADD, 0x1c0u, "__sync_fetch_and_add") \
    X(SYNC_FETCH_AND_SUB, 0x1c1u, "__sync_fetch_and_sub") \
    X(SYNC_FETCH_AND_OR, 0x1c2u, "__sync_fetch_and_or") \
    X(SYNC_FETCH_AND_AND, 0x1c3u, "__sync_fetch_and_and") \
    X(SYNC_FETCH_AND_XOR, 0x1c4u, "__sync_fetch_and_xor") \
    X(SYNC_FETCH_AND_NAND, 0x1c5u, "__sync_fetch_and_nand") \
    X(SYNC_ADD_AND_FETCH, 0x1c6u, "__sync_add_and_fetch") \
    X(SYNC_SUB_AND_FETCH, 0x1c7u, "__sync_sub_and_fetch") \
    X(SYNC_OR_AND_FETCH, 0x1c8u, "__sync_or_and_fetch") \
    X(SYNC_AND_AND_FETCH, 0x1c9u, "__sync_and_and_fetch") \
    X(SYNC_XOR_AND_FETCH, 0x1cau, "__sync_xor_and_fetch") \
    X(SYNC_NAND_AND_FETCH, 0x1cbu, "__sync_nand_and_fetch") \
    X(SYNC_BOOL_COMPARE_AND_SWAP, 0x1ccu, "__sync_bool_compare_and_swap") \
    X(SYNC_VAL_COMPARE_AND_SWAP, 0x1cdu, "__sync_val_compare_and_swap") \
    X(SYNC_SYNCHRONIZE, 0x1ceu, "__sync_synchronize") \
    X(SYNC_LOCK_TEST_AND_SET, 0x1cfu, "__sync_lock_test_and_set") \
    X(SYNC_LOCK_RELEASE, 0x1d0u, "__sync_lock_release") \
    X(FRAME_ADDRESS, 0x1f0u, "__builtin_frame_address") \
    X(RETURN_ADDRESS, 0x1f1u, "__builtin_return_address") \
    X(EXTRACT_RETURN_ADDR, 0x1f2u, "__builtin_extract_return_addr") \
    X(FROB_RETURN_ADDR, 0x1f3u, "__builtin_frob_return_addr") \
    X(ELEMENTWISE_ABS, 0x300u, "__builtin_elementwise_abs") \
    X(ELEMENTWISE_ADD_SAT, 0x301u, "__builtin_elementwise_add_sat") \
    X(ELEMENTWISE_BITREVERSE, 0x302u, "__builtin_elementwise_bitreverse") \
    X(ELEMENTWISE_CLZG, 0x303u, "__builtin_elementwise_clzg") \
    X(ELEMENTWISE_FMA, 0x304u, "__builtin_elementwise_fma") \
    X(ELEMENTWISE_FSHL, 0x305u, "__builtin_elementwise_fshl") \
    X(ELEMENTWISE_FSHR, 0x306u, "__builtin_elementwise_fshr") \
    X(ELEMENTWISE_MAX, 0x307u, "__builtin_elementwise_max") \
    X(ELEMENTWISE_MIN, 0x308u, "__builtin_elementwise_min") \
    X(ELEMENTWISE_POPCOUNT, 0x309u, "__builtin_elementwise_popcount") \
    X(ELEMENTWISE_SQRT, 0x30au, "__builtin_elementwise_sqrt") \
    X(ELEMENTWISE_SUB_SAT, 0x30bu, "__builtin_elementwise_sub_sat") \
    X(REDUCE_ADD, 0x30cu, "__builtin_reduce_add") \
    X(REDUCE_AND, 0x30du, "__builtin_reduce_and") \
    X(REDUCE_MAX, 0x30eu, "__builtin_reduce_max") \
    X(REDUCE_MIN, 0x30fu, "__builtin_reduce_min") \
    X(REDUCE_MUL, 0x310u, "__builtin_reduce_mul") \
    X(REDUCE_OR, 0x311u, "__builtin_reduce_or") \
    X(SHUFFLEVECTOR, 0x312u, "__builtin_shufflevector") \
    X(CONVERTVECTOR, 0x313u, "__builtin_convertvector") \
    X(NONDETERMINISTIC_VALUE, 0x314u, "__builtin_nondeterministic_value") \
    X(NONTEMPORAL_LOAD, 0x315u, "__builtin_nontemporal_load") \
    X(NONTEMPORAL_STORE, 0x316u, "__builtin_nontemporal_store") \
    X(BIT_CAST, 0x317u, "__builtin_bit_cast")

typedef enum CBuiltinId
{
    CBUILTIN_NONE,
#define BUSTER_CBUILTIN_ENUM(id, name) CBUILTIN_##id,
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_ENUM)
#undef BUSTER_CBUILTIN_ENUM
    // Integer action sentinel: existing action loops and vector descriptors stop here.
    CBUILTIN_COUNT,
#define BUSTER_CBUILTIN_SPECIAL_ENUM(id, value, name) CBUILTIN_##id = value,
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
#define BUSTER_CBUILTIN_INTEGER_ENTRY(id, name) { CBUILTIN_##id, name, sizeof(name) - 1, BUSTER_CBUILTIN_CAP_FOLD },
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_INTEGER_ENTRY)
#undef BUSTER_CBUILTIN_INTEGER_ENTRY
#define BUSTER_CBUILTIN_SPECIAL_ENTRY(id, value, name) { CBUILTIN_##id, name, sizeof(name) - 1, 0 },
    BUSTER_CBUILTIN_SPECIAL_NAMES(BUSTER_CBUILTIN_SPECIAL_ENTRY)
#undef BUSTER_CBUILTIN_SPECIAL_ENTRY
};

_Static_assert(CBUILTIN_BSWAP16 == 1 && CBUILTIN_FFSLL == 18 && CBUILTIN_ADD_OVERFLOW == 19 && CBUILTIN_MUL_OVERFLOW == 21 && CBUILTIN_COUNT == 22,
               "integer builtin action identities remain stable");
_Static_assert(CBUILTIN_VA_START == 0x100 && CBUILTIN_VA_END == 0x104, "special builtin identities remain outside integer actions");

// Binding and feature queries resolve bounded exact spellings. The integer action layer's SIMD/scalar lookups
// deliberately retain their old integer-only contract. A name's bytes are inspected only after its length matches.
// Every catalog name starts with "__" (test-preprocessor checks it), so a name that doesn't is no builtin's, which the
// checker asks of each distinct identifier of a unit (#529).
static __attribute__((unused)) CBuiltinId cbuiltin_resolve(const char* name, uint32_t length)
{
    CBuiltinId result = CBUILTIN_NONE;
    if (name != 0 && length >= 2 && length < BUSTER_CBUILTIN_MAX_NAME && name[0] == '_' && name[1] == '_')
    {
        const uint32_t count = sizeof(cbuiltin_catalog) / sizeof(cbuiltin_catalog[0]);
        for (uint32_t i = 0; i < count && result == CBUILTIN_NONE; i += 1)
        {
            if (length == cbuiltin_catalog[i].length && memcmp(name, cbuiltin_catalog[i].name, length) == 0)
            {
                result = cbuiltin_catalog[i].id;
            }
        }
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
        required |= BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0 | BUSTER_CBUILTIN_CAP_TIER1;
    }
    if (name != 0 && required != 0 && (required & ~allowed) == 0 && length != 0 && length < BUSTER_CBUILTIN_MAX_NAME)
    {
        const uint32_t count = sizeof(cbuiltin_catalog) / sizeof(cbuiltin_catalog[0]);
        for (uint32_t i = 0; i < count && !result; i += 1)
        {
            result = (cbuiltin_catalog[i].capabilities & required) == required && length == cbuiltin_catalog[i].length &&
                     memcmp(name, cbuiltin_catalog[i].name, length) == 0;
        }
    }
    return result;
}

#endif
