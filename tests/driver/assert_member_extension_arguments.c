// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define BUSTER_WRAP(x) x
struct Wrapped { int before; BUSTER_WRAP(__extension__ _Static_assert(1, "ignored")); };
#define BUSTER_EXT __extension__
#define BUSTER_ASSERT _Static_assert
#define BUSTER_TOGETHER BUSTER_EXT BUSTER_ASSERT(1, "ignored")
struct Nested { int before; BUSTER_TOGETHER; };
#define BUSTER_EXTENSION(x) __extension__ x
struct Argument { int before; BUSTER_EXTENSION(_Static_assert(1, "ignored")); };
