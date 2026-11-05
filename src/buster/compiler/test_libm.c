// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The host libraries' functions and objects that the tests and benchmarks use as oracles (libm's), and that the
// interpreter's programs call where dlsym(RTLD_DEFAULT) doesn't find them (ssa_source_test.c, interpret-ir's --bind)
// (#652). The compiler's link takes libm only where something still references it (host_math.h has the compiler's own
// operations), and with BUSTER_HOST_STATIC links statically, where dlsym(RTLD_DEFAULT) finds nothing:
//   - linked dynamically, from libm.so.6 loaded on the first lookup, whose dependencies (libc.so.6) dlsym searches
//     too. The process has one libm, this one or the one its link took, so its objects (signgam) are those its
//     functions set;
//   - linked statically, from the table below first, which links in each function and object it names, the C
//     library's that c-testsuite's programs and the generated ones call and libm's that the tests compare with, so
//     that they are this process's own (its errno, its stdout). Any other name comes from libm.so.6 and the C library
//     it brings, loaded as a linked-dynamically executable loads it: a second C library, with its own errno, stdio
//     buffers and heap, which still binds every name the dynamic executable bound (interpret-ir's --bind contract).
#ifndef BUSTER_TEST_LIBM_INCLUDED
#define BUSTER_TEST_LIBM_INCLUDED

// Whether this executable is linked statically (BUSTER_HOST_STATIC, cmake/BusterSettings.cmake): then nothing is
// preloaded into it, and the tests that count its threads read the lanes' own count alone (lane_threads_report).
#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
#define BUSTER_HOST_STATIC_EXECUTABLE 1
#else
#define BUSTER_HOST_STATIC_EXECUTABLE 0
#endif

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TestHostSymbol TestHostSymbol;
struct TestHostSymbol
{
    const char* name;
    void* address;
};

#define BUSTER_TEST_HOST_SYMBOL(name) { #name, (void*)&name }
static TestHostSymbol test_host_symbols[] = {
    // The C library's: what c-testsuite's programs, the generated ones and tests/ir's call.
    BUSTER_TEST_HOST_SYMBOL(printf),
    BUSTER_TEST_HOST_SYMBOL(fprintf),
    BUSTER_TEST_HOST_SYMBOL(sprintf),
    BUSTER_TEST_HOST_SYMBOL(snprintf),
    BUSTER_TEST_HOST_SYMBOL(puts),
    BUSTER_TEST_HOST_SYMBOL(putchar),
    BUSTER_TEST_HOST_SYMBOL(fputs),
    BUSTER_TEST_HOST_SYMBOL(fputc),
    BUSTER_TEST_HOST_SYMBOL(fwrite),
    BUSTER_TEST_HOST_SYMBOL(fread),
    BUSTER_TEST_HOST_SYMBOL(fopen),
    BUSTER_TEST_HOST_SYMBOL(fclose),
    BUSTER_TEST_HOST_SYMBOL(fflush),
    BUSTER_TEST_HOST_SYMBOL(fgets),
    BUSTER_TEST_HOST_SYMBOL(fgetc),
    BUSTER_TEST_HOST_SYMBOL(getc),
    BUSTER_TEST_HOST_SYMBOL(getchar),
    BUSTER_TEST_HOST_SYMBOL(ungetc),
    BUSTER_TEST_HOST_SYMBOL(malloc),
    BUSTER_TEST_HOST_SYMBOL(calloc),
    BUSTER_TEST_HOST_SYMBOL(realloc),
    BUSTER_TEST_HOST_SYMBOL(free),
    BUSTER_TEST_HOST_SYMBOL(memset),
    BUSTER_TEST_HOST_SYMBOL(memcpy),
    BUSTER_TEST_HOST_SYMBOL(memmove),
    BUSTER_TEST_HOST_SYMBOL(memcmp),
    BUSTER_TEST_HOST_SYMBOL(memchr),
    BUSTER_TEST_HOST_SYMBOL(strlen),
    BUSTER_TEST_HOST_SYMBOL(strcpy),
    BUSTER_TEST_HOST_SYMBOL(strncpy),
    BUSTER_TEST_HOST_SYMBOL(strcat),
    BUSTER_TEST_HOST_SYMBOL(strncat),
    BUSTER_TEST_HOST_SYMBOL(strcmp),
    BUSTER_TEST_HOST_SYMBOL(strncmp),
    BUSTER_TEST_HOST_SYMBOL(strchr),
    BUSTER_TEST_HOST_SYMBOL(strrchr),
    BUSTER_TEST_HOST_SYMBOL(strstr),
    BUSTER_TEST_HOST_SYMBOL(strtol),
    BUSTER_TEST_HOST_SYMBOL(strtoul),
    BUSTER_TEST_HOST_SYMBOL(strtod),
    BUSTER_TEST_HOST_SYMBOL(atoi),
    BUSTER_TEST_HOST_SYMBOL(atol),
    BUSTER_TEST_HOST_SYMBOL(abs),
    BUSTER_TEST_HOST_SYMBOL(labs),
    BUSTER_TEST_HOST_SYMBOL(exit),
    BUSTER_TEST_HOST_SYMBOL(abort),
    BUSTER_TEST_HOST_SYMBOL(qsort),
    BUSTER_TEST_HOST_SYMBOL(stdin),
    BUSTER_TEST_HOST_SYMBOL(stdout),
    BUSTER_TEST_HOST_SYMBOL(stderr),
    // libm's: the programs' (00174.c's sin), the math helpers' oracles (c_builtin_math_helpers_test.c), host_math.h's
    // oracles and the other tests'.
    BUSTER_TEST_HOST_SYMBOL(sin),
    BUSTER_TEST_HOST_SYMBOL(cos),
    BUSTER_TEST_HOST_SYMBOL(tan),
    BUSTER_TEST_HOST_SYMBOL(exp),
    BUSTER_TEST_HOST_SYMBOL(log),
    BUSTER_TEST_HOST_SYMBOL(pow),
    BUSTER_TEST_HOST_SYMBOL(fabs),
    BUSTER_TEST_HOST_SYMBOL(fmod),
    BUSTER_TEST_HOST_SYMBOL(sqrt),
    BUSTER_TEST_HOST_SYMBOL(sqrtf),
    BUSTER_TEST_HOST_SYMBOL(copysign),
    BUSTER_TEST_HOST_SYMBOL(copysignf),
    BUSTER_TEST_HOST_SYMBOL(ceil),
    BUSTER_TEST_HOST_SYMBOL(ceilf),
    BUSTER_TEST_HOST_SYMBOL(floor),
    BUSTER_TEST_HOST_SYMBOL(floorf),
    BUSTER_TEST_HOST_SYMBOL(trunc),
    BUSTER_TEST_HOST_SYMBOL(truncf),
    BUSTER_TEST_HOST_SYMBOL(nearbyint),
    BUSTER_TEST_HOST_SYMBOL(nearbyintf),
    BUSTER_TEST_HOST_SYMBOL(rint),
    BUSTER_TEST_HOST_SYMBOL(rintf),
    BUSTER_TEST_HOST_SYMBOL(lgamma),
    BUSTER_TEST_HOST_SYMBOL(signgam),
    BUSTER_TEST_HOST_SYMBOL(ilogb),
    BUSTER_TEST_HOST_SYMBOL(ldexp),
    BUSTER_TEST_HOST_SYMBOL(ldexpl),
    BUSTER_TEST_HOST_SYMBOL(frexpl),
    BUSTER_TEST_HOST_SYMBOL(fegetround),
    BUSTER_TEST_HOST_SYMBOL(fesetround),
    BUSTER_TEST_HOST_SYMBOL(fegetenv),
    BUSTER_TEST_HOST_SYMBOL(fesetenv),
    BUSTER_TEST_HOST_SYMBOL(feclearexcept),
    BUSTER_TEST_HOST_SYMBOL(fetestexcept),
};
#undef BUSTER_TEST_HOST_SYMBOL
#endif

// The address of `name`, a function or object of libm or the C library, or null if it can't be found.
static __attribute__((unused)) void* test_libm(const char* name)
{
    void* result = NULL;
#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
    for (size_t i = 0; result == NULL && i < sizeof(test_host_symbols) / sizeof(test_host_symbols[0]); i += 1)
    {
        result = strcmp(test_host_symbols[i].name, name) == 0 ? test_host_symbols[i].address : NULL;
    }
#endif
    static void* library = NULL;
    if (result == NULL && library == NULL)
    {
        library = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    }
    result = result == NULL && library != NULL ? dlsym(library, name) : result;
    return result;
}

#endif
