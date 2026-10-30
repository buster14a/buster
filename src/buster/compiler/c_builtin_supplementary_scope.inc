// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The x86 builtins of the source scope past the frozen own-main inventory (docs/builtin-metadata/own-main.names.txt):
// #799's immediate forms and #148's others, which no build of this compiler calls. Clang 23.1.1's metadata supplies their
// prototypes, c_check_builtin_x86.c their audited SemaX86 constraints, and each name's second field is its required target
// features in that metadata, which __has_builtin evaluates (#148). This does not alter the frozen inventory.
//
// The names are in strcmp's order, which the scope's lookups search (builtin_intrinsic_supplementary_in_scope and
// cbuiltin_source_scope), and none is the inventory's: test-builtin-intrinsic-catalog checks both, and
// test-preprocessor --clang each name's features against the metadata's.
BUSTER_CBUILTIN_SUPPLEMENTARY_NAME("__builtin_ia32_palignr256", "avx2")
BUSTER_CBUILTIN_SUPPLEMENTARY_NAME("__builtin_ia32_pblendw128", "sse4.1")
BUSTER_CBUILTIN_SUPPLEMENTARY_NAME("__builtin_ia32_permdi256", "avx2")
BUSTER_CBUILTIN_SUPPLEMENTARY_NAME("__builtin_ia32_pshufd256", "avx2")
BUSTER_CBUILTIN_SUPPLEMENTARY_NAME("__builtin_ia32_shufpd", "sse2")
