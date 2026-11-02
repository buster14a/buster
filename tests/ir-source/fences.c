// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's fences in the IR: every order and scope, in a loop, in an expression and among loads and stores of a global,
// each of a constant order and so taken directly (the typed stream's FENCE, the IR's fence) rather than by a helper's
// call, which the IR's interpreter has no code for. test-ir-source lowers them by both paths, promotes them and reads
// their text back; test-ssa-source runs the program in the IR's three forms against clang -O0's build's output.
#include <stdio.h>

static int shared;

static int ordered(int o)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    shared += o;
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    return shared;
}

int main(void)
{
    int total = 0;
    for (int i = 0; i < 4; i++)
    {
        shared += i;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        total += shared;
        __atomic_signal_fence(__ATOMIC_ACQUIRE);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        total += (__atomic_thread_fence(__ATOMIC_ACQ_REL), i);
    }
    __sync_synchronize();
    __c11_atomic_thread_fence(__ATOMIC_CONSUME);
    __c11_atomic_signal_fence(__ATOMIC_RELAXED);
    total += ordered(__ATOMIC_SEQ_CST);
    printf("%d\n", total);
    return 0;
}
