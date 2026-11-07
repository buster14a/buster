// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #1374/#1652: the source IR benchmark must keep both command-line and per-function target features. No generated code
// executes here, so baseline hosts can check the feature instructions as well as the baseline recipes.

#define BIT_BUILTINS(x, y, z) \
    (__builtin_ctz((x) | 1u) + __builtin_clz((x) | 1u) + __builtin_popcount(x) + __builtin_parity(x) + __builtin_ffs(x) + \
     __builtin_ctzl((y) | 1ul) + __builtin_clzl((y) | 1ul) + __builtin_popcountl(y) + __builtin_parityl(y) + __builtin_ffsl(y) + \
     __builtin_ctzll((z) | 1ull) + __builtin_clzll((z) | 1ull) + __builtin_popcountll(z) + __builtin_parityll(z) + __builtin_ffsll(z) + \
     __builtin_bswap16(x) + __builtin_bswap32(x) + __builtin_bswap64(z))

#define GENERIC_BITS(x, y, z) \
    (__builtin_popcountg((unsigned char)(x)) + __builtin_clzg((unsigned short)(x), -7) + __builtin_ctzg((unsigned _BitInt(37))(y), -9) + \
     __builtin_stdc_leading_zeros(x) + __builtin_stdc_trailing_zeros(y) + __builtin_stdc_count_ones(z) + \
     __builtin_stdc_bit_width(x) + __builtin_stdc_bit_floor(y) + __builtin_stdc_bit_ceil(z) + \
     __builtin_bswapg((unsigned short)(x)) + __builtin_bswapg((unsigned _BitInt(48))(y)))

unsigned long long bits_default(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z) + GENERIC_BITS(x, y, z);
}

__attribute__((target("bmi,lzcnt,popcnt")))
unsigned long long bits_features(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z) + GENERIC_BITS(x, y, z);
}

__attribute__((target("no-bmi,no-lzcnt,no-popcnt")))
unsigned long long bits_baseline(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z) + GENERIC_BITS(x, y, z);
}

unsigned long bits_generic_wide(unsigned long low, unsigned long high, int fallback)
{
    const unsigned __int128 u = ((unsigned __int128)high << 64) | low;
    const unsigned _BitInt(65) a = u;
    const unsigned _BitInt(96) b = u;
    const unsigned _BitInt(127) c = u;
    unsigned long result = __builtin_popcountg(a) + __builtin_popcountg(b) + __builtin_popcountg(c);
    result = result * 31 + __builtin_clzg(a, fallback) + __builtin_ctzg(a, fallback);
    result = result * 31 + __builtin_clzg(b, fallback) + __builtin_ctzg(b, fallback);
    result = result * 31 + __builtin_clzg(c, fallback) + __builtin_ctzg(c, fallback);
    result = result * 31 + (unsigned long)__builtin_stdc_bit_floor(u) + (unsigned long)__builtin_stdc_bit_ceil(u);
    result = result * 31 + (unsigned long)__builtin_bswapg(u);
    return result;
}
