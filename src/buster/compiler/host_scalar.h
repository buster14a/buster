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
// X(value) for each of the 256 entries, which makes both tables below.
#define HOST_AES_SBOX(X) \
    X(0x63) X(0x7c) X(0x77) X(0x7b) X(0xf2) X(0x6b) X(0x6f) X(0xc5) X(0x30) X(0x01) X(0x67) X(0x2b) X(0xfe) X(0xd7) X(0xab) X(0x76) \
    X(0xca) X(0x82) X(0xc9) X(0x7d) X(0xfa) X(0x59) X(0x47) X(0xf0) X(0xad) X(0xd4) X(0xa2) X(0xaf) X(0x9c) X(0xa4) X(0x72) X(0xc0) \
    X(0xb7) X(0xfd) X(0x93) X(0x26) X(0x36) X(0x3f) X(0xf7) X(0xcc) X(0x34) X(0xa5) X(0xe5) X(0xf1) X(0x71) X(0xd8) X(0x31) X(0x15) \
    X(0x04) X(0xc7) X(0x23) X(0xc3) X(0x18) X(0x96) X(0x05) X(0x9a) X(0x07) X(0x12) X(0x80) X(0xe2) X(0xeb) X(0x27) X(0xb2) X(0x75) \
    X(0x09) X(0x83) X(0x2c) X(0x1a) X(0x1b) X(0x6e) X(0x5a) X(0xa0) X(0x52) X(0x3b) X(0xd6) X(0xb3) X(0x29) X(0xe3) X(0x2f) X(0x84) \
    X(0x53) X(0xd1) X(0x00) X(0xed) X(0x20) X(0xfc) X(0xb1) X(0x5b) X(0x6a) X(0xcb) X(0xbe) X(0x39) X(0x4a) X(0x4c) X(0x58) X(0xcf) \
    X(0xd0) X(0xef) X(0xaa) X(0xfb) X(0x43) X(0x4d) X(0x33) X(0x85) X(0x45) X(0xf9) X(0x02) X(0x7f) X(0x50) X(0x3c) X(0x9f) X(0xa8) \
    X(0x51) X(0xa3) X(0x40) X(0x8f) X(0x92) X(0x9d) X(0x38) X(0xf5) X(0xbc) X(0xb6) X(0xda) X(0x21) X(0x10) X(0xff) X(0xf3) X(0xd2) \
    X(0xcd) X(0x0c) X(0x13) X(0xec) X(0x5f) X(0x97) X(0x44) X(0x17) X(0xc4) X(0xa7) X(0x7e) X(0x3d) X(0x64) X(0x5d) X(0x19) X(0x73) \
    X(0x60) X(0x81) X(0x4f) X(0xdc) X(0x22) X(0x2a) X(0x90) X(0x88) X(0x46) X(0xee) X(0xb8) X(0x14) X(0xde) X(0x5e) X(0x0b) X(0xdb) \
    X(0xe0) X(0x32) X(0x3a) X(0x0a) X(0x49) X(0x06) X(0x24) X(0x5c) X(0xc2) X(0xd3) X(0xac) X(0x62) X(0x91) X(0x95) X(0xe4) X(0x79) \
    X(0xe7) X(0xc8) X(0x37) X(0x6d) X(0x8d) X(0xd5) X(0x4e) X(0xa9) X(0x6c) X(0x56) X(0xf4) X(0xea) X(0x65) X(0x7a) X(0xae) X(0x08) \
    X(0xba) X(0x78) X(0x25) X(0x2e) X(0x1c) X(0xa6) X(0xb4) X(0xc6) X(0xe8) X(0xdd) X(0x74) X(0x1f) X(0x4b) X(0xbd) X(0x8b) X(0x8a) \
    X(0x70) X(0x3e) X(0xb5) X(0x66) X(0x48) X(0x03) X(0xf6) X(0x0e) X(0x61) X(0x35) X(0x57) X(0xb9) X(0x86) X(0xc1) X(0x1d) X(0x9e) \
    X(0xe1) X(0xf8) X(0x98) X(0x11) X(0x69) X(0xd9) X(0x8e) X(0x94) X(0x9b) X(0x1e) X(0x87) X(0xe9) X(0xce) X(0x55) X(0x28) X(0xdf) \
    X(0x8c) X(0xa1) X(0x89) X(0x0d) X(0xbf) X(0xe6) X(0x42) X(0x68) X(0x41) X(0x99) X(0x2d) X(0x0f) X(0xb0) X(0x54) X(0xbb) X(0x16)

#define HOST_AES_BYTE(value) value,
static const uint8_t host_aes_sbox[256] = {HOST_AES_SBOX(HOST_AES_BYTE)};

// An AES round's MixColumns of one substituted byte s in row 0 of its column, as the column's little-endian word:
// rows 0 to 3 get 2s, s, s and 3s. A byte in row r contributes the same word rotated left by 8r bits, so that a
// round is four lookups and a rotation each per column, rather than a doubling per byte (#2658: the scalar host's
// emulated vaesenc was 77% of test-snapshot under MSan). The compiler computes the words from the S-box above.
#define HOST_AES_TWICE(value) ((((value) << 1) ^ (((value) >> 7) * 0x1b)) & 0xff)
#define HOST_AES_MIX(value) \
    ((uint32_t)HOST_AES_TWICE(value) | ((uint32_t)(value) << 8) | ((uint32_t)(value) << 16) | \
     ((uint32_t)(HOST_AES_TWICE(value) ^ (value)) << 24)),
static const uint32_t host_aes_mix[256] = {HOST_AES_SBOX(HOST_AES_MIX)};

BUSTER_HOST_INLINE uint32_t host_aes_rotate(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32 - bits));
}

BUSTER_HOST_INLINE __m128i host_aesenc(__m128i value, __m128i key)
{
    uint8_t input[16];
    uint32_t round_key[4];
    uint32_t output[4];
    memcpy(input, &value, 16);
    memcpy(round_key, &key, 16);
    for (unsigned column = 0; column < 4; column += 1)
    {
        // ShiftRows: row r of the column comes from column + r.
        const uint32_t row0 = host_aes_mix[input[4 * column]];
        const uint32_t row1 = host_aes_rotate(host_aes_mix[input[4 * ((column + 1) & 3) + 1]], 8);
        const uint32_t row2 = host_aes_rotate(host_aes_mix[input[4 * ((column + 2) & 3) + 2]], 16);
        const uint32_t row3 = host_aes_rotate(host_aes_mix[input[4 * ((column + 3) & 3) + 3]], 24);
        output[column] = row0 ^ row1 ^ row2 ^ row3 ^ round_key[column];
    }
    __m128i result;
    memcpy(&result, output, 16);
    return result;
}
#if !defined(__AES__)
#define _mm_aesenc_si128(value, key) host_aesenc((value), (key))
#endif
#endif

