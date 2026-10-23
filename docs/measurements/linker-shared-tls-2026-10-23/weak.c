// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original TLS ABI probe for issue #85.
extern __thread int buster_absent __attribute__((weak, visibility("hidden")));
int* buster_lookup(void)
{
    return &buster_absent;
}
