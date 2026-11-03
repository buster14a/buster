// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's atomic loads and stores in the IR: every width and order, GNU's _n and C11's, a pointer's, and an implicit
// _Atomic object's, each aligned and of a constant order and so taken directly (the typed stream's marked LOAD and SET,
// and a seq_cst store's FENCE; the IR's atomic load and store, and its fence) rather than by a helper's call, which the
// IR's interpreter has no code for. Promotion leaves every atomic access in memory. test-ir-source lowers them by both
// paths, promotes them and reads their text back; test-ssa-source runs the program in the IR's three forms against
// clang -O0's build's output.
#include <stdio.h>

static _Atomic int counter;
static _Atomic char flag;
static long wide;
static short narrow;
static int* pointer;

static int published(int value)
{
    __atomic_store_n(&wide, (long)value << 33, __ATOMIC_SEQ_CST);
    __atomic_store_n(&narrow, (short)value, __ATOMIC_RELEASE);
    flag = 1;
    return (int)(__atomic_load_n(&wide, __ATOMIC_ACQUIRE) >> 33) + __atomic_load_n(&narrow, __ATOMIC_RELAXED) + flag;
}

int main(void)
{
    int local = 0;
    int total = 0;
    for (int i = 0; i < 4; i++)
    {
        __atomic_store_n(&local, i * 3, __ATOMIC_RELAXED);
        total += __atomic_load_n(&local, __ATOMIC_SEQ_CST);
        counter = total;
        total += counter;
        __c11_atomic_store(&counter, i, __ATOMIC_RELEASE);
        total += __c11_atomic_load(&counter, __ATOMIC_CONSUME);
    }
    __atomic_store_n(&pointer, &local, __ATOMIC_RELEASE);
    total += *__atomic_load_n(&pointer, __ATOMIC_ACQUIRE);
    total += published(total);
    printf("%d\n", total);
    return 0;
}
