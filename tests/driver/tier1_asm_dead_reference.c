// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The dead asm unit's label remains the target of a live static-data fixup: direct linking must name it as undefined.
extern char buster2351_dead_reference[];
char* volatile buster2351_retained_reference = buster2351_dead_reference;

int main(void)
{
    int dead = 0;
    if (dead)
    {
        __asm__ volatile(".p2align 4\nbuster2351_dead_reference:\n\tnop");
    }
    return buster2351_retained_reference != 0;
}
