// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The temporary directory the tests build their trees in (#1104): $TMPDIR (or /tmp) with runs of slashes collapsed and
// trailing slashes removed, so that a path a test spells and a path a tool resolves from its working directory name the
// same bytes. A value too long for the buffer is returned raw, and the oracle cache then leaves its roots off rather
// than truncating a path. This thread's buffer, valid until the next call here.
#ifndef BUSTER_TEST_PATHS_INCLUDED
#define BUSTER_TEST_PATHS_INCLUDED

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_TEST_TEMPORARY_BYTES 4096

// The buffer, one for the whole executable: main.c's unit defines it, and every other object, a unit of main.c or a
// benchmark's object of its own, declares it. Every object that includes this file is linked with main.c's. A buffer of
// each object's own was 19 of the executable's 22 pages of zero thread-local data, which the C library clears at every
// thread's start, the process's first among them: 18 of an empty file's faults on the 9700X (#1444).
#if defined(BUSTER_UNIT) && BUSTER_UNIT == BUSTER_UNIT_DRIVER
_Thread_local char oracle_temporary_buffer[BUSTER_TEST_TEMPORARY_BYTES];
#else
extern _Thread_local char oracle_temporary_buffer[BUSTER_TEST_TEMPORARY_BYTES];
#endif

static inline __attribute__((unused)) const char* oracle_temporary_directory(void)
{
    char* directory = oracle_temporary_buffer;
    const char* base = getenv("TMPDIR");
    base = base != 0 && base[0] != 0 ? base : "/tmp";
    size_t length = 0;
    bool slash = false;
    for (size_t at = 0; base[at] != 0; at += 1)
    {
        if (base[at] == '/')
        {
            if (slash && length != 0)
            {
                continue;
            }
            slash = true;
        }
        else
        {
            slash = false;
        }
        if (length + 1 >= BUSTER_TEST_TEMPORARY_BYTES)
        {
            return base;
        }
        directory[length] = base[at];
        length += 1;
    }
    while (length > 1 && directory[length - 1] == '/')
    {
        length -= 1;
    }
    directory[length] = 0;
    return directory;
}

#endif
