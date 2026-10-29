// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Deliberate faults, each in its own process. CI checks both the diagnostic and
// exit status; a crash, a missing runtime or an optimized-away fault cannot pass.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile unsigned probe_index = 1;
static volatile unsigned probe_racy_value;
static const volatile uint8_t* probe_escaped;

static __attribute__((noinline)) void probe_escape(void)
{
    volatile uint8_t bytes[16] = { 7 };
    probe_escaped = bytes;
}

static __attribute__((noinline)) void probe_leak(void)
{
    volatile uint8_t* bytes = malloc(113);
    if (bytes != 0)
    {
        bytes[0] = 7;
    }
}

static void* probe_race(void* argument)
{
    pthread_barrier_t* barrier = argument;
    pthread_barrier_wait(barrier);
    for (unsigned i = 0; i < 100000; i += 1)
    {
        probe_racy_value += 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;
    if (argc != 2)
    {
        result = 2;
    }
    else if (strcmp(argv[1], "clean") == 0)
    {
        uint8_t* bytes = calloc(16, 1);
        if (bytes == 0)
        {
            result = 2;
        }
        else
        {
            bytes[probe_index] = 7;
            result = bytes[probe_index] != 7;
            free(bytes);
        }
    }
    else if (strcmp(argv[1], "heap-overflow") == 0)
    {
        volatile uint8_t* bytes = malloc(1);
        if (bytes == 0)
        {
            result = 2;
        }
        else
        {
            bytes[probe_index] = 7;
            free((void*)bytes);
        }
    }
    else if (strcmp(argv[1], "use-after-free") == 0)
    {
        volatile uint8_t* bytes = malloc(16);
        if (bytes == 0)
        {
            result = 2;
        }
        else
        {
            free((void*)bytes);
            bytes[probe_index] = 7;
        }
    }
    else if (strcmp(argv[1], "use-after-return") == 0)
    {
        probe_escape();
        result = *probe_escaped != 7;
    }
    else if (strcmp(argv[1], "leak") == 0)
    {
        probe_leak();
    }
    else if (strcmp(argv[1], "undefined-shift") == 0)
    {
        result = (1u << (probe_index + 31)) != 0;
    }
    else if (strcmp(argv[1], "bounds") == 0)
    {
        unsigned values[4] = { 1, 2, 3, 4 };
        result = values[probe_index + 4] != 0;
    }
    else if (strcmp(argv[1], "race") == 0)
    {
        pthread_barrier_t barrier;
        pthread_t thread;
        if (pthread_barrier_init(&barrier, 0, 2) != 0)
        {
            result = 2;
        }
        else if (pthread_create(&thread, 0, probe_race, &barrier) != 0)
        {
            pthread_barrier_destroy(&barrier);
            result = 2;
        }
        else
        {
            probe_race(&barrier);
            pthread_join(thread, 0);
            pthread_barrier_destroy(&barrier);
        }
    }
    else if (strcmp(argv[1], "uninitialized") == 0)
    {
        volatile unsigned* value = malloc(sizeof(*value));
        if (value == 0)
        {
            result = 2;
        }
        else
        {
            result = *value != 0;
            free((void*)value);
        }
    }
    else
    {
        result = 2;
    }
    return result;
}
