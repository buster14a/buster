// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for #1115's whole-program -S corpus (assembler_programs_test.c): thread-local variables of every TLS
// model (local-exec by default; initial-exec and general-dynamic by attribute, which GCC writes as @gottpoff and
// @tlsgd; general- and local-dynamic with -fPIC), threads, tentative definitions (.comm under -fcommon) and static
// zero-initialized storage (GCC's .local and .comm). No generated assembly is checked in.

#include <pthread.h>
#include <stdio.h>

_Thread_local int storage_counter = 5;
static _Thread_local long storage_hidden;
_Thread_local char storage_buffer[24];
__attribute__((tls_model("initial-exec"))) _Thread_local int storage_initial = 11;
__attribute__((tls_model("global-dynamic"))) _Thread_local long storage_dynamic = 13;

// Tentative definitions: common symbols under -fcommon.
int storage_tentative;
long storage_tentative_array[50];
double storage_tentative_doubles[3];

static int storage_zeroed;
static double storage_zeroed_doubles[8];
static char storage_zeroed_bytes[3];

static void* storage_work(void* argument)
{
    const long id = (long)argument;
    for (long i = 0; i < 1000; i += 1)
    {
        storage_counter += 1;
        storage_hidden += id;
        storage_initial += 2;
        storage_dynamic -= 1;
    }
    storage_buffer[id] = (char)('a' + id);
    return (void*)(storage_hidden + storage_counter + storage_buffer[id] + storage_initial + storage_dynamic);
}

int main(void)
{
    pthread_t threads[4];
    long results[5] = { 0 };
    int created = 0;
    for (long t = 0; t < 4; t += 1)
    {
        created += pthread_create(&threads[t], 0, storage_work, (void*)t) == 0;
    }
    for (long t = 0; t < created; t += 1)
    {
        void* result = 0;
        pthread_join(threads[t], &result);
        results[t] = (long)result;
    }
    results[4] = (long)storage_work((void*)5);
    storage_tentative += 7;
    storage_tentative_array[49] = storage_tentative * 3;
    storage_tentative_doubles[2] = 0.5;
    storage_zeroed += 3;
    storage_zeroed_doubles[7] = 2.5;
    storage_zeroed_bytes[2] = 'z';
    printf("%ld %ld %ld %ld %ld %d %ld %.1f %d %.1f %c %d %ld %d %ld\n", results[0], results[1], results[2], results[3], results[4], storage_tentative,
           storage_tentative_array[49], storage_tentative_doubles[2], storage_zeroed, storage_zeroed_doubles[7], storage_zeroed_bytes[2], storage_counter,
           storage_hidden, storage_initial, storage_dynamic);
    // Each thread's variables start from their initial values: 1005 + id * 1000 + 'a' + id + 2011 + (13 - 1000), the
    // main thread's id 5.
    long expected = 0;
    for (long t = 0; t < 5; t += 1)
    {
        const long id = t < 4 ? t : 5;
        expected += results[t] == 1005 + id * 1000 + 'a' + id + 2011 + 13 - 1000;
    }
    return created == 4 && expected == 5 && storage_tentative == 7 && storage_zeroed == 3 ? 0 : 1;
}
