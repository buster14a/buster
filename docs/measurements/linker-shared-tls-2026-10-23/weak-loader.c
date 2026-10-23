// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original TLS ABI probe for issue #85.
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char** argv)
{
    void* library = argc == 2 ? dlopen(argv[1], RTLD_NOW | RTLD_LOCAL) : 0;
    int* (*lookup)(void) = library != 0 ? (int* (*)(void))dlsym(library, "buster_lookup") : 0;
    void* value = lookup != 0 ? lookup() : (void*)1;
    printf("lookup=%p value=%p error=%s\n", (void*)lookup, value, library == 0 ? dlerror() : "none");
    return value != 0;
}
