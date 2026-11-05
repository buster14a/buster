// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// libm's functions and objects for the tests and benchmarks that use them as oracles, and for the natives that the
// interpreter's programs call (ssa_source_test.c, interpret-ir's --bind) where dlsym(RTLD_DEFAULT) doesn't find them
// (#652): the compiler's link takes libm only where something still references it (host_math.h has the compiler's
// own), so they reach it here, from libm.so.6 loaded on the first lookup. The process has one libm, this or the one its link took, so its objects (signgam) are those its functions set.
#ifndef BUSTER_TEST_LIBM_INCLUDED
#define BUSTER_TEST_LIBM_INCLUDED

#include <dlfcn.h>
#include <stddef.h>

// The address of libm's `name`, or null if libm or the name can't be found.
static __attribute__((unused)) void* test_libm(const char* name)
{
    static void* library = NULL;
    if (library == NULL)
    {
        library = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    }
    return library != NULL ? dlsym(library, name) : NULL;
}

#endif
