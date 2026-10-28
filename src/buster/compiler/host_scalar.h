// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Scalar host operations used when optional x86 extensions are absent.
// SSE2 is part of the x86-64 baseline; these routines require no later ISA.
#ifndef BUSTER_HOST_SCALAR_H
#define BUSTER_HOST_SCALAR_H
#include <stdint.h>
#include <string.h>

#define BUSTER_HOST_INLINE static inline __attribute__((always_inline, unused))

BUSTER_HOST_INLINE uint64_t host_bzhi(uint64_t value, unsigned index)
{
    return index >= 64 ? value : value & ((UINT64_C(1) << index) - 1);
}

BUSTER_HOST_INLINE uint64_t host_pdep(uint64_t value, uint64_t mask)
{
    uint64_t result = 0;
    uint64_t bit = 1;
    while (mask != 0)
    {
        const uint64_t lowest = mask & (0 - mask);
        result |= (value & bit) != 0 ? lowest : 0;
        mask &= mask - 1;
        bit <<= 1;
    }
    return result;
}

BUSTER_HOST_INLINE uint64_t host_pext(uint64_t value, uint64_t mask)
{
    uint64_t result = 0;
    uint64_t bit = 1;
    while (mask != 0)
    {
        const uint64_t lowest = mask & (0 - mask);
        result |= (value & lowest) != 0 ? bit : 0;
        mask &= mask - 1;
        bit <<= 1;
    }
    return result;
}

#if !defined(__BMI2__)
#define _bzhi_u64(value, index) host_bzhi((value), (uint8_t)(index))
#define _bzhi_u32(value, index) ((uint32_t)host_bzhi((uint32_t)(value), (uint8_t)(index)))
#define _pdep_u64(value, mask) host_pdep((value), (mask))
#define _pdep_u32(value, mask) ((uint32_t)host_pdep((uint32_t)(value), (uint32_t)(mask)))
#define _pext_u64(value, mask) host_pext((value), (mask))
#define _pext_u32(value, mask) ((uint32_t)host_pext((uint32_t)(value), (uint32_t)(mask)))
#endif
#if !defined(__BMI__)
BUSTER_HOST_INLINE uint64_t host_blsmsk64(uint64_t value)
{
    return value ^ (value - 1);
}
BUSTER_HOST_INLINE uint32_t host_blsmsk32(uint32_t value)
{
    return value ^ (value - 1u);
}
#undef _blsmsk_u64
#undef _blsmsk_u32
#undef __blsmsk_u64
#undef __blsmsk_u32
#define _blsmsk_u64(value) host_blsmsk64(value)
#define _blsmsk_u32(value) host_blsmsk32(value)
#define __blsmsk_u64(value) host_blsmsk64(value)
#define __blsmsk_u32(value) host_blsmsk32(value)
BUSTER_HOST_INLINE unsigned host_ctz64(uint64_t value)
{
    return value != 0 ? (unsigned)__builtin_ctzll(value) : 64u;
}
BUSTER_HOST_INLINE unsigned host_ctz32(uint32_t value)
{
    return value != 0 ? (unsigned)__builtin_ctz(value) : 32u;
}
#undef _tzcnt_u64
#undef _tzcnt_u32
#define _tzcnt_u64(value) host_ctz64(value)
#define _tzcnt_u32(value) host_ctz32(value)
#endif

BUSTER_HOST_INLINE uint64_t host_mask_shift64(uint64_t value, unsigned count)
{
    return count >= 64 ? 0 : value << count;
}
BUSTER_HOST_INLINE uint16_t host_mask_shift16(uint16_t value, unsigned count)
{
    return count >= 16 ? 0 : (uint16_t)((uint32_t)value << count);
}
BUSTER_HOST_INLINE uint64_t host_load_mask64(const void* memory)
{
    uint64_t result;
    memcpy(&result, memory, sizeof(result));
    return result;
}
#undef _cvtu64_mask64
#undef _cvtmask64_u64
#undef _kadd_mask16
#undef _kand_mask16
#undef _mm512_kand
#undef _kor_mask64
#undef _kxor_mask64
#undef _ktestz_mask64_u8
#undef _kshiftli_mask16
#undef _kshiftli_mask64
#undef _load_mask64
#define _cvtu64_mask64(value) ((__mmask64)(value))
#define _cvtmask64_u64(value) ((uint64_t)(value))
#define _kadd_mask16(a, b) ((__mmask16)((uint32_t)(a) + (uint32_t)(b)))
#define _kand_mask16(a, b) ((__mmask16)((a) & (b)))
#define _mm512_kand(a, b) ((__mmask16)((a) & (b)))
#define _kor_mask64(a, b) ((__mmask64)((a) | (b)))
#define _kxor_mask64(a, b) ((__mmask64)((a) ^ (b)))
#define _ktestz_mask64_u8(a, b) ((unsigned char)(((a) & (b)) == 0))
#define _kshiftli_mask16(value, count) host_mask_shift16((value), (uint8_t)(count))
#define _kshiftli_mask64(value, count) host_mask_shift64((value), (uint8_t)(count))
#define _load_mask64(memory) host_load_mask64(memory)

#if !defined(__XSAVE__)
// Callers check CPUID.OSXSAVE before reading XCR0. Keep the instruction in
// that guarded path without requiring XSAVE for the entire baseline build.
BUSTER_HOST_INLINE uint64_t host_xgetbv(unsigned index)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("xgetbv" : "=a"(low), "=d"(high) : "c"(index));
    return (uint64_t)low | (uint64_t)high << 32;
}
#undef _xgetbv
#define _xgetbv(index) host_xgetbv(index)
#endif
#if !defined(__LZCNT__)
BUSTER_HOST_INLINE unsigned host_clz64(uint64_t value)
{
    return value != 0 ? (unsigned)__builtin_clzll(value) : 64u;
}
#define _lzcnt_u64(value) host_clz64(value)
#endif

#if !defined(__POPCNT__)
BUSTER_HOST_INLINE long long host_popcnt64(unsigned long long value)
{
    return __builtin_popcountll(value);
}
#undef _mm_popcnt_u64
#define _mm_popcnt_u64(value) host_popcnt64(value)
#endif

// Generated directly from Rijndael's GF(2^8) inverse and affine transform,
// using polynomial 0x11b and affine constant 0x63. No upstream table is copied.
static const uint8_t host_aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

BUSTER_HOST_INLINE uint8_t host_aes_twice(uint8_t value)
{
    return (uint8_t)((value << 1) ^ ((value >> 7) * 0x1b));
}

BUSTER_HOST_INLINE __m128i host_aesenc(__m128i value, __m128i key)
{
    uint8_t input[16];
    uint8_t shifted[16];
    uint8_t output[16];
    uint8_t round_key[16];
    memcpy(input, &value, 16);
    memcpy(round_key, &key, 16);
    for (unsigned column = 0; column < 4; column += 1)
    {
        for (unsigned row = 0; row < 4; row += 1)
        {
            shifted[4 * column + row] = host_aes_sbox[input[4 * ((column + row) & 3) + row]];
        }
        const uint8_t* a = shifted + 4 * column;
        const uint8_t sum = a[0] ^ a[1] ^ a[2] ^ a[3];
        for (unsigned row = 0; row < 4; row += 1)
        {
            output[4 * column + row] = a[row] ^ sum ^ host_aes_twice(a[row] ^ a[(row + 1) & 3]) ^ round_key[4 * column + row];
        }
    }
    __m128i result;
    memcpy(&result, output, 16);
    return result;
}
#if !defined(__AES__)
#define _mm_aesenc_si128(value, key) host_aesenc((value), (key))
#endif
#endif

