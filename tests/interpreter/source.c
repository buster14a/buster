// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Keep the interpreter's 520-byte aggregate; the native variant uses 64 longs for Tier 1's 512-byte argument limit.
#ifndef BUSTER_INTERPRETER_LARGE_WORDS
#define BUSTER_INTERPRETER_LARGE_WORDS 65
#endif

typedef struct Pair Pair;
struct Pair
{
    long first;
    long second;
};

typedef struct Large Large;
struct Large
{
    long values[BUSTER_INTERPRETER_LARGE_WORDS];
};

typedef struct Aligned Aligned;
struct Aligned
{
    long value;
} __attribute__((aligned(128)));

static long collect_aligned(int ignored, ...)
{
    va_list values;
    va_start(values, ignored);
    const Aligned value = va_arg(values, Aligned);
    va_end(values);
    return value.value;
}

static long collect_register_only(long first, ...)
{
    va_list values;
    va_start(values, first);
    const long second = va_arg(values, long);
    const long third = va_arg(values, long);
    va_end(values);
    return first + second + third;
}

static Pair change_pair(Pair value)
{
    value.first += 7;
    value.second *= 3;
    return value;
}

static Large change_large(Large value)
{
    value.values[4] += value.values[0];
    value.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] += value.values[0];
    return value;
}

static long collect(int count, ...)
{
    va_list values;
    va_start(values, count);
    long total = 0;
    for (int i = 0; i < count; i += 1)
    {
        total += va_arg(values, long);
    }
    const double fraction = va_arg(values, double);
    const long double extended = va_arg(values, long double);
    const Pair pair = va_arg(values, Pair);
    va_end(values);
    return total + (long)fraction + (long)extended + pair.first + pair.second;
}

static long stacked(long a, long b, long c, long d, long e, long f, long g, ...)
{
    va_list values;
    va_start(values, g);
    const long h = va_arg(values, long);
    const long i = va_arg(values, long);
    va_end(values);
    return a + b + c + d + e + f + g + h + i;
}

static int labels(int index)
{
    static void* targets[] = { &&first, &&second };
    goto *targets[index];
first:
    return 17;
second:
    return 29;
}

int main(void)
{
    Pair original = { 11, 13 };
    Pair (*indirect)(Pair) = change_pair;
    const Pair changed = indirect(original);
    Large large = { { 2, 3, 5, 7, 11, [BUSTER_INTERPRETER_LARGE_WORDS - 1] = 23 } };
    const Large returned = change_large(large);
    // The outer initializer owns its return storage; the nested call still needs a separate result object (#1549).
    const Large nested = change_large(change_large(large));
    Large (*large_indirect)(Large) = change_large;
    const Large indirect_large = large_indirect(large);
    Large assigned = large;
    assigned = change_large(assigned);
    const div_t quotient = div(20, 6);
    const long sum = collect(8, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 2.5, 4.75L, original);
    const long tail = stacked(1, 2, 3, 4, 5, 6, 7, 8L, 9L);
    const Aligned aligned = { 161 };
    char bytes[64];
    memset(bytes, 3, sizeof(bytes));
    memmove(bytes + 1, bytes, sizeof(bytes) - 1);
    // Forty ints are 160 bytes, past the 128-byte unrolled-initializer limit, so this is a pooled memcpy.
    int pooled[40] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
        31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    };
    const int passed = pooled[0] == 1 && pooled[19] == 20 && pooled[39] == 40 &&
                       original.first == 11 && original.second == 13 && changed.first == 18 && changed.second == 39 &&
                       large.values[4] == 11 && returned.values[4] == 13 && returned.values[32] == 0 &&
                       large.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] == 23 && returned.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] == 25 &&
                       nested.values[4] == 15 && nested.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] == 27 &&
                       indirect_large.values[4] == 13 && indirect_large.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] == 25 &&
                       assigned.values[4] == 13 && assigned.values[BUSTER_INTERPRETER_LARGE_WORDS - 1] == 25 &&
                       quotient.quot == 3 && quotient.rem == 2 &&
                       sum == 66 && tail == 45 && collect_register_only(3, 5L, 7L) == 15 && collect_aligned(0, aligned) == 161 && labels(0) == 17 && labels(1) == 29 && bytes[63] == 3;
    printf("typed source: %d\n", passed);
    return passed ? 0 : 1;
}
