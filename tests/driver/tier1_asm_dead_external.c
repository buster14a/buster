// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An undefined external referenced only by the eliminated asm unit must not reach the direct binder.
int main(void)
{
    int dead = 0;
    if (dead)
    {
        __asm__ volatile(".p2align 4\n\tnop\n\t.pushsection .rodata.buster2351_dead_external, \"a\"\n"
                         "\t.quad buster2351_dead_external\n\t.popsection");
    }
    return 0;
}
