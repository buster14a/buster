// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Direct linking must discard the eliminated asm unit's promises for its declared code label and asm-only data label.
#include "../checker/programs/asm-directive-placement/directives.c"
#include <stdio.h>

int main(void)
{
    for (int n = -3; n < 20; n += 1)
    {
        printf("%d\n", tested(n));
    }
    return 0;
}
