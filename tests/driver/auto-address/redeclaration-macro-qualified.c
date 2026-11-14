// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define SPACE(N) __attribute__((address_space(N)))
extern const int SPACE(2) *volatile *value;
const SPACE(3) __auto_type *volatile *value = (const int SPACE(3) *volatile *)0;
