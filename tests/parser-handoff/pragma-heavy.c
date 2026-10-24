// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The benchmark's 512 pragma events need their own capacity despite only three parser tokens.
#define PAIR _Pragma("pack(push, 1)") _Pragma("pack(pop)")
#define PAIR_2 PAIR PAIR
#define PAIR_4 PAIR_2 PAIR_2
#define PAIR_8 PAIR_4 PAIR_4
#define PAIR_16 PAIR_8 PAIR_8
#define PAIR_32 PAIR_16 PAIR_16
#define PAIR_64 PAIR_32 PAIR_32
#define PAIR_128 PAIR_64 PAIR_64
#define PAIR_256 PAIR_128 PAIR_128
PAIR_256
int x;
