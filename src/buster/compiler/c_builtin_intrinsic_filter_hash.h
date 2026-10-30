// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared by the catalog's immutable negative filter and its C generator. The first/last four logical
// bytes and length form a bounded fingerprint, not an identity: middle-only changes deliberately collide.
// Only the exact catalog lookup can accept a name. Host and generator use the Linux/x86 little-endian ABI.
#ifndef BUSTER_C_BUILTIN_INTRINSIC_FILTER_HASH_INCLUDED
#define BUSTER_C_BUILTIN_INTRINSIC_FILTER_HASH_INCLUDED
#include <stdint.h>
#include <string.h>

#define BUSTER_BUILTIN_INTRINSIC_FILTER_BITS 65536u
#define BUSTER_BUILTIN_INTRINSIC_FILTER_WORDS (BUSTER_BUILTIN_INTRINSIC_FILTER_BITS / 64u)

// Nonnull, nonempty, bounded name; no termination or readable padding. Short names use byte loads.
static uint32_t builtin_intrinsic_filter_key(const char* name, uint32_t length)
{
    uint32_t first = 0;
    uint32_t last = 0;
    if (length >= sizeof(first))
    {
        memcpy(&first, name, sizeof(first));
        memcpy(&last, name + length - sizeof(last), sizeof(last));
    }
    else
    {
        for (uint32_t i = 0; i < length; i += 1)
        {
            first |= (uint32_t)(uint8_t)name[i] << (8 * i);
        }
        last = first;
    }
    const uint32_t key = first ^ ((last << 13) | (last >> 19)) ^ (length * UINT32_C(0x9e3779b9));
    return key;
}

static uint32_t builtin_intrinsic_filter_hash(uint32_t key)
{
    const uint32_t mixed = (key ^ (key >> 16)) * UINT32_C(0x7feb352d);
    const uint32_t result = (mixed ^ (mixed >> 15)) & (BUSTER_BUILTIN_INTRINSIC_FILTER_BITS - 1u);
    return result;
}
#endif
