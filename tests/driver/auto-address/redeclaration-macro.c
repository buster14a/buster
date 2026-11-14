// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define SPACE __attribute__((address_space(2)))
extern int value;
SPACE __auto_type value = 2;
