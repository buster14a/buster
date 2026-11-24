// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int memcmp(const void*, const void*, unsigned long);
void* memmove(void*, const void*, unsigned long);

int compare(const char* left, const char* right, unsigned long size)
{
    return memcmp(left, right, BYTES);
}

#ifdef CLONES
__attribute__((target_clones("arch=x86-64", "default")))
#endif
void* move(char* destination, const char* source, unsigned long size)
{
    return memmove(destination, source, BYTES);
}
