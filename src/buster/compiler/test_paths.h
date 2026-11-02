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

static inline __attribute__((unused)) const char* oracle_temporary_directory(void)
{
    static _Thread_local char directory[BUSTER_TEST_TEMPORARY_BYTES];
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
        if (length + 1 >= sizeof(directory))
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
