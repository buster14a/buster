// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define SPACE(N) __attribute__((address_space(N)))
const SPACE(4) restrict __auto_type value = (int *)0;
