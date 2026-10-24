// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original header-heavy input for benchmark-checker-bindings (#336): most of its tokens and binding events come
// from the C library's headers, as a typical translation unit's do, and a few functions use and shadow them.
#define _GNU_SOURCE 1
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

typedef struct Buffer Buffer;
struct Buffer
{
    char* bytes;
    size_t length;
    size_t capacity;
};

static int count_digits(const char* text, size_t length)
{
    int count = 0;
    for (size_t i = 0; i < length; i += 1)
    {
        count += isdigit((unsigned char)text[i]) != 0;
    }
    return count;
}

static bool buffer_append(Buffer* buffer, const char* text)
{
    const size_t length = strlen(text);
    bool fits = buffer->length + length <= buffer->capacity;
    if (fits)
    {
        memcpy(buffer->bytes + buffer->length, text, length);
        buffer->length += length;
    }
    return fits;
}

int describe(const char* path, Buffer* buffer)
{
    struct stat status;
    int result = stat(path, &status);
    if (result == 0)
    {
        char line[128];
        int length = snprintf(line, sizeof line, "%" PRIu64 " bytes, mode %o\n", (uint64_t)status.st_size, (unsigned)status.st_mode);
        {
            int result = count_digits(line, (size_t)length);
            length = result > 0 ? length : 0;
        }
        result = buffer_append(buffer, line) ? 0 : ENOSPC;
    }
    else
    {
        result = errno;
    }
    return result;
}

double scaled(double value, int exponent)
{
    double result = ldexp(value, exponent);
    return isfinite(result) ? result : DBL_MAX;
}
