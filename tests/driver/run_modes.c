// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#if defined(BUSTER_RUN_MODE_REJECT)
#error rejected run mode fixture
#endif

int run_mode_value(void)
{
    return 17;
}
