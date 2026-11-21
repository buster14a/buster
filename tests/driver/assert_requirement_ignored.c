// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Ignored attributes disappear from the requirement's canonical type; warning policy is tested separately.
_Static_assert(_Generic((int *[[gnu::unused]])0, int *: 1, default: 0) == 2, "ignored declaration attribute");
_Static_assert(_Generic((int *[[unknown::decoration]])0, int *: 1, default: 0) == 2, "unknown attribute");
