// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #1374: the source IR benchmark must keep both command-line and per-function target features. No generated code
// executes here, so baseline hosts can check the feature instructions as well as the baseline recipes.

#define BIT_BUILTINS(x, y, z) \
    (__builtin_ctz((x) | 1u) + __builtin_clz((x) | 1u) + __builtin_popcount(x) + __builtin_parity(x) + __builtin_ffs(x) + \
     __builtin_ctzl((y) | 1ul) + __builtin_clzl((y) | 1ul) + __builtin_popcountl(y) + __builtin_parityl(y) + __builtin_ffsl(y) + \
     __builtin_ctzll((z) | 1ull) + __builtin_clzll((z) | 1ull) + __builtin_popcountll(z) + __builtin_parityll(z) + __builtin_ffsll(z) + \
     __builtin_bswap16(x) + __builtin_bswap32(x) + __builtin_bswap64(z))

unsigned long long bits_default(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z);
}

__attribute__((target("bmi,lzcnt,popcnt")))
unsigned long long bits_features(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z);
}

__attribute__((target("no-bmi,no-lzcnt,no-popcnt")))
unsigned long long bits_baseline(unsigned int x, unsigned long y, unsigned long long z)
{
    return BIT_BUILTINS(x, y, z);
}
