// SPDX-License-Identifier: BSD-2-Clause
//
// xxHash's XXH3 64-bit hash, with seed 0, of inputs of at most 128 bytes: a C translation of llvm::xxh3_64bits's paths
// for those lengths, llvm/lib/Support/xxhash.cpp at llvm-project commit c47d7823685a (clang 23.1.6's), which LLVM bases
// on xxHash commit d5891596637d21366b9b1dcf2c0007a3edb26a9e. Clang's StringMap hashes its keys with it (#2093,
// c_check_feature_map.c). Local changes: C, seed 0 alone, lengths past 128 bytes left out (the caller refuses them),
// little-endian loads by memcpy, and one return per function. See THIRD_PARTY_NOTICES.md.
//
//  xxHash - Extremely Fast Hash algorithm
//  Copyright (C) 2012-2023, Yann Collet
//
//  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//  * Redistributions of source code must retain the above copyright
//  notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above
//  copyright notice, this list of conditions and the following disclaimer
//  in the documentation and/or other materials provided with the
//  distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  You can contact the author at :
//  - xxHash homepage: http://www.xxhash.com
//  - xxHash source repository : https://github.com/Cyan4973/xxHash

#include <stdint.h>
#include <string.h>

#define BUSTER_XXH3_SHORT_MAX 128u

// Pseudorandom data taken directly from FARSH: the first 128 bytes of XXH3's default secret, all that these lengths read.
static const uint8_t xxh3_secret[128] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
};

static uint64_t xxh3_read64(const uint8_t* bytes)
{
    uint64_t value = 0;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint32_t xxh3_read32(const uint8_t* bytes)
{
    uint32_t value = 0;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint64_t xxh3_rotl64(uint64_t x, uint32_t r)
{
    return (x << r) | (x >> (64 - r));
}

static uint64_t xxh3_avalanche64(uint64_t hash)
{
    hash ^= hash >> 33;
    hash *= 14029467366897019727ull;
    hash ^= hash >> 29;
    hash *= 1609587929392839161ull;
    hash ^= hash >> 32;
    return hash;
}

static uint64_t xxh3_avalanche(uint64_t hash)
{
    hash ^= hash >> 37;
    hash *= 0x165667919E3779F9ull;
    hash ^= hash >> 32;
    return hash;
}

static uint64_t xxh3_fold64(uint64_t lhs, uint64_t rhs)
{
    const unsigned __int128 product = (unsigned __int128)lhs * (unsigned __int128)rhs;
    return (uint64_t)product ^ (uint64_t)(product >> 64);
}

static uint64_t xxh3_mix16(const uint8_t* input, const uint8_t* secret)
{
    return xxh3_fold64(xxh3_read64(secret) ^ xxh3_read64(input), xxh3_read64(secret + 8) ^ xxh3_read64(input + 8));
}

// XXH3_64bits(input, length) for a length of at most BUSTER_XXH3_SHORT_MAX bytes.
static uint64_t xxh3_short(const uint8_t* input, uint32_t length)
{
    const uint8_t* secret = xxh3_secret;
    uint64_t hash = 0;
    if (length > 16)
    {
        uint64_t acc = (uint64_t)length * 11400714785074694791ull;
        uint64_t end = 0;
        acc += xxh3_mix16(input, secret);
        end = xxh3_mix16(input + length - 16, secret + 16);
        for (uint32_t step = 1; step < 4 && length > 32u * step; step += 1)
        {
            acc += xxh3_mix16(input + 16 * step, secret + 32 * step);
            end += xxh3_mix16(input + length - 16 * (step + 1), secret + 32 * step + 16);
        }
        hash = xxh3_avalanche(acc + end);
    }
    else if (length > 8)
    {
        const uint64_t low = (xxh3_read64(secret + 24) ^ xxh3_read64(secret + 32)) ^ xxh3_read64(input);
        const uint64_t high = (xxh3_read64(secret + 40) ^ xxh3_read64(secret + 48)) ^ xxh3_read64(input + length - 8);
        hash = xxh3_avalanche((uint64_t)length + __builtin_bswap64(low) + high + xxh3_fold64(low, high));
    }
    else if (length >= 4)
    {
        const uint64_t input64 = (uint64_t)xxh3_read32(input + length - 4) | ((uint64_t)xxh3_read32(input) << 32);
        uint64_t acc = (xxh3_read64(secret + 8) ^ xxh3_read64(secret + 16)) ^ input64;
        acc ^= xxh3_rotl64(acc, 49) ^ xxh3_rotl64(acc, 24);
        acc *= 0x9FB21C651E98DF25ull;
        acc ^= (acc >> 35) + (uint64_t)length;
        acc *= 0x9FB21C651E98DF25ull;
        hash = acc ^ (acc >> 28);
    }
    else if (length != 0)
    {
        const uint32_t combined = ((uint32_t)input[0] << 16) | ((uint32_t)input[length >> 1] << 24) | (uint32_t)input[length - 1] | (length << 8);
        hash = xxh3_avalanche64((uint64_t)combined ^ (uint64_t)(xxh3_read32(secret) ^ xxh3_read32(secret + 4)));
    }
    else
    {
        hash = xxh3_avalanche64(xxh3_read64(secret + 56) ^ xxh3_read64(secret + 64));
    }
    return hash;
}
