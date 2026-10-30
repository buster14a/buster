// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exact supplementary source scope for #799. Clang 23.1.1's metadata supplies their prototypes; the separately
// audited immediate constraints are in c_check_builtin_x86.c. This does not alter the frozen own-main inventory.
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_palignr256")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_pblendw128")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_permdi256")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_pshufd256")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_shufpd")
