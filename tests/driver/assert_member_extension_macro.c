// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define BUSTER_EXTENSION __extension__
#define BUSTER_ASSERT _Static_assert
struct Macro { int before; BUSTER_EXTENSION BUSTER_ASSERT(1, "ignored"); };
#define BUSTER_MEMBER __extension__ _Static_assert(0, "unread failure")
struct FullMacro { int before; BUSTER_MEMBER; };
