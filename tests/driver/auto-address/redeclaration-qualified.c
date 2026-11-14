// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
extern int **value;
const __attribute__((address_space(3))) __auto_type **value = 0;
