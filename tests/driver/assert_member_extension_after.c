// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct Rejected { int before; __extension__ _Static_assert(0, "unread failure"); };
struct After { int valid; _Static_assert(1, "valid member"); };
int later(int value) { return value + ; }
