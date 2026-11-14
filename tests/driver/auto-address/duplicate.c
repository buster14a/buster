// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
__attribute__((address_space(4))) __attribute__((address_space(4))) __auto_type value = 1;
_Static_assert(_Generic(&value, int __attribute__((address_space(4))) *: 1, default: 0), "duplicate space");
