#pragma once

#include <buster/lib/base.h>

BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE u64 align_forward_unchecked(u64 n, u64 a)
{
    // Callers prove that `a` is a nonzero power of two and that the rounding
    // addition cannot overflow before using this low-cost form.
    u64 result = (n + a - 1) & ~(a - 1);
    return result;
}

// Returns false for a null output or an overflowing addition; on false,
// `*result` is untouched.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE bool u64_add_checked(u64 left, u64 right, u64* result)
{
    bool valid = result && left <= UINT64_MAX - right;
    if (valid)
    {
        *result = left + right;
    }

    return valid;
}

// Returns false for a null output, zero or non-power-of-two alignment, or a
// rounding addition that would overflow; on false, `*result` is untouched.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE bool align_forward_checked(u64 n, u64 a, u64* result)
{
    bool valid = result && BUSTER_IS_POWER_OF_TWO(a) && n <= UINT64_MAX - (a - 1);
    if (valid)
    {
        *result = align_forward_unchecked(n, a);
    }

    return valid;
}

BUSTER_F_DECL u64 align_forward(u64 n, u64 a);
BUSTER_F_DECL bool is_aligned(u64 n, u64 alignment);
BUSTER_F_DECL u64 next_power_of_two(u64 n);
// Zero input returns the operand width: 32 for u32 and 64 for u64.
BUSTER_F_DECL u8 trailing_zeroes_u32(u32 n);
BUSTER_F_DECL u8 trailing_zeroes_u64(u64 n);
BUSTER_F_DECL u8 leading_zeroes_u32(u32 n);
BUSTER_F_DECL u8 leading_zeroes_u64(u64 n);
