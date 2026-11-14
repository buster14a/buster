// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define SPACE(N) __attribute__((address_space(N)))
extern int value;
const _Atomic __auto_type value SPACE(3) = 1;
