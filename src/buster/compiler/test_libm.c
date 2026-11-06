// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The host libraries' functions and objects that the tests and benchmarks use as oracles (libm's), and that the
// interpreter's programs call where dlsym(RTLD_DEFAULT) doesn't find them (ssa_source_test.c, interpret-ir's --bind)
// (#652). The compiler's link takes libm only where something still references it (host_math.h has the compiler's own
// operations), and with BUSTER_HOST_STATIC links statically, where dlsym(RTLD_DEFAULT) finds nothing:
//   - linked dynamically, from libm.so.6 loaded on the first lookup, whose dependencies (libc.so.6) dlsym searches
//     too. The process has one libm and one C library, this one or the ones its link took, so a name binds to the
//     functions and objects whose state (signgam, errno, the heap, stdio's files) is the process's own;
//   - linked statically, from the table below alone, which links in each function and object it names: the C library's
//     that c-testsuite's programs, the generated ones and tests/ir call, its functions with the process-wide state
//     they share (the heap, stdio and the files it makes, the environment, errno, the random numbers, strtok, the exit
//     handlers, the signals, the locale and the time zone), and libm's that the tests compare with. Any other name
//     binds to nothing: a libm.so.6 loaded into a static executable brings a second C library, whose state is its own,
//     so that its tmpfile's FILE would be another C library's to this one's fwrite and fclose, and its errno another
//     variable. A name the interpreter's programs need there goes in the table.
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
#include <stdlib.h>
#include <string.h>

#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

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
    BUSTER_TEST_HOST_SYMBOL(div),
    BUSTER_TEST_HOST_SYMBOL(exit),
    BUSTER_TEST_HOST_SYMBOL(abort),
    BUSTER_TEST_HOST_SYMBOL(qsort),
    BUSTER_TEST_HOST_SYMBOL(stdin),
    BUSTER_TEST_HOST_SYMBOL(stdout),
    BUSTER_TEST_HOST_SYMBOL(stderr),
    // The C library's process-wide state beyond the heap and stdio above, which a second C library would keep apart:
    // the environment, errno, the random numbers, strtok's position, the exit handlers, the signals, the locale and the
    // time zone.
    BUSTER_TEST_HOST_SYMBOL(getenv),
    BUSTER_TEST_HOST_SYMBOL(setenv),
    BUSTER_TEST_HOST_SYMBOL(unsetenv),
    BUSTER_TEST_HOST_SYMBOL(putenv),
    BUSTER_TEST_HOST_SYMBOL(clearenv),
    BUSTER_TEST_HOST_SYMBOL(environ),
    BUSTER_TEST_HOST_SYMBOL(__errno_location),
    BUSTER_TEST_HOST_SYMBOL(perror),
    BUSTER_TEST_HOST_SYMBOL(rand),
    BUSTER_TEST_HOST_SYMBOL(srand),
    BUSTER_TEST_HOST_SYMBOL(random),
    BUSTER_TEST_HOST_SYMBOL(srandom),
    BUSTER_TEST_HOST_SYMBOL(drand48),
    BUSTER_TEST_HOST_SYMBOL(srand48),
    BUSTER_TEST_HOST_SYMBOL(strtok),
    BUSTER_TEST_HOST_SYMBOL(atexit),
    BUSTER_TEST_HOST_SYMBOL(signal),
    BUSTER_TEST_HOST_SYMBOL(raise),
    BUSTER_TEST_HOST_SYMBOL(setlocale),
    BUSTER_TEST_HOST_SYMBOL(localeconv),
    BUSTER_TEST_HOST_SYMBOL(tzset),
    BUSTER_TEST_HOST_SYMBOL(localtime),
    BUSTER_TEST_HOST_SYMBOL(gmtime),
    BUSTER_TEST_HOST_SYMBOL(mktime),
    BUSTER_TEST_HOST_SYMBOL(strftime),
    BUSTER_TEST_HOST_SYMBOL(time),
    BUSTER_TEST_HOST_SYMBOL(setvbuf),
    BUSTER_TEST_HOST_SYMBOL(ferror),
    BUSTER_TEST_HOST_SYMBOL(feof),
    BUSTER_TEST_HOST_SYMBOL(clearerr),
    BUSTER_TEST_HOST_SYMBOL(fseek),
    BUSTER_TEST_HOST_SYMBOL(ftell),
    BUSTER_TEST_HOST_SYMBOL(rewind),
    BUSTER_TEST_HOST_SYMBOL(scanf),
    BUSTER_TEST_HOST_SYMBOL(fscanf),
    BUSTER_TEST_HOST_SYMBOL(sscanf),
    // The files stdio makes, which only this C library's functions may read, write and close.
    BUSTER_TEST_HOST_SYMBOL(tmpfile),
    BUSTER_TEST_HOST_SYMBOL(fdopen),
    BUSTER_TEST_HOST_SYMBOL(freopen),
    BUSTER_TEST_HOST_SYMBOL(fmemopen),
    BUSTER_TEST_HOST_SYMBOL(open_memstream),
    BUSTER_TEST_HOST_SYMBOL(fileno),
    // The kernel's file descriptors, through this C library's calls, whose errno is the process's (test-ssa-source's
    // self-test closes them all), and the process's identity, which tools/ci/correctness.tests binds.
    BUSTER_TEST_HOST_SYMBOL(open),
    BUSTER_TEST_HOST_SYMBOL(close),
    BUSTER_TEST_HOST_SYMBOL(read),
    BUSTER_TEST_HOST_SYMBOL(write),
    BUSTER_TEST_HOST_SYMBOL(lseek),
    BUSTER_TEST_HOST_SYMBOL(dup),
    BUSTER_TEST_HOST_SYMBOL(dup2),
    BUSTER_TEST_HOST_SYMBOL(pipe),
    BUSTER_TEST_HOST_SYMBOL(unlink),
    BUSTER_TEST_HOST_SYMBOL(getpid),
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

// The address of `name`, a function or object of libm or the C library, or null if it can't be found: in a static
// executable, if the table doesn't name it.
static __attribute__((unused)) void* test_libm(const char* name)
{
    void* result = NULL;
#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
    for (size_t i = 0; result == NULL && i < sizeof(test_host_symbols) / sizeof(test_host_symbols[0]); i += 1)
    {
        result = strcmp(test_host_symbols[i].name, name) == 0 ? test_host_symbols[i].address : NULL;
    }
#else
    static void* library = NULL;
    if (library == NULL)
    {
        library = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    }
    result = library != NULL ? dlsym(library, name) : NULL;
    // A dynamic executable's C library can supply atexit through a hidden nonshared entry, which dlsym cannot find.
    // Its linked address still registers with this process's C library and preserves its own exit-handler state.
    if (result == NULL && strcmp(name, "atexit") == 0)
    {
        result = (void*)&atexit;
    }
#endif
    return result;
}

#endif
