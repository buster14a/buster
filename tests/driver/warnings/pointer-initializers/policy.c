// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
const char fixed[2];
char *first[1] = { fixed };
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
char *quiet[1] = { fixed };
#pragma clang diagnostic pop
char *last[1] = { fixed };
