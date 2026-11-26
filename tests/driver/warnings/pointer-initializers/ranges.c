// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
const char fixed[2];
char *repeated[5] = { [0 ... 4] = fixed };
struct S { char *p; };
struct S object = (struct S){ fixed };
