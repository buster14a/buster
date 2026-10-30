// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exact supplementary source scope for #799. Clang 23.1.1's metadata supplies their prototypes; the separately
// audited immediate constraints are in c_check_builtin_x86.c. This does not alter the frozen own-main inventory. Each
// name's second field is its required target features in that metadata, which __has_builtin evaluates (#148).
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_palignr256", "avx2")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_pblendw128", "sse4.1")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_permdi256", "avx2")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_pshufd256", "avx2")
BUSTER_CBUILTIN_IMMEDIATE_NAME("__builtin_ia32_shufpd", "sse2")
