// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
_Static_assert(0 << 1, "ignored");
#pragma clang diagnostic warning "-Wtautological-constant-compare"
_Static_assert(0 << 2, "warning");
#pragma clang diagnostic error "-Wtautological-constant-compare"
_Static_assert(0 << 3, "promoted");
#pragma clang diagnostic pop
_Static_assert(0 << 4, "restored");
