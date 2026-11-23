// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3329: a volatile vector's loads and stores (16 bytes, and 64 with AVX-512) and a volatile bool vector's copies reach
// the IR as volatile accesses, as a scalar's do (#3310, volatiles.c), whose typed value's bytes the mark leaves as they
// are. Value numbering with memory kept a vector global's second read as its first's value and the value a store through
// a pointer to a volatile vector stored as the next reads'. The program runs in the IR's forms and Tier 1 against clang
// (the typed stream's interpreter runs no vector); correctness.tests counts the printed volatile accesses and each
// function's loads and stores under --tier1-passes=+all against the default passes', at x86-64-v4 too.
#include <stdio.h>

typedef int Lanes __attribute__((vector_size(16)));
typedef int WideLanes __attribute__((vector_size(64)));
typedef _Bool Truths __attribute__((ext_vector_type(16)));
volatile Lanes lanes;
volatile WideLanes wide_lanes;
volatile Truths truths;

__attribute__((noinline)) int vector(void)
{
    Lanes a = lanes;
    Lanes b = lanes;
    return a[0] + b[1];
}

__attribute__((noinline)) int vector_through(volatile Lanes* p)
{
    *p = (Lanes){ 1, 2, 3, 4 };
    *p = (Lanes){ 5, 6, 7, 8 };
    Lanes a = *p;
    Lanes b = *p;
    return a[2] + b[3];
}

// The IR takes a 64-byte vector with AVX-512 alone.
#if defined(__AVX512F__)
__attribute__((noinline)) int vector_wide(void)
{
    WideLanes a = wide_lanes;
    WideLanes b = wide_lanes;
    return a[0] + b[15];
}
#else
__attribute__((noinline)) int vector_wide(void)
{
    return 0;
}
#endif

__attribute__((noinline)) int truth_vector(void)
{
    Truths a = truths;
    Truths b = truths;
    return a[0] + b[1];
}

int main(void)
{
    Lanes local = { 0, 0, 0, 0 };
    lanes = (Lanes){ 9, 10, 11, 12 };
    truths = (Truths){ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
    int checked = vector() + vector_through(&local) + vector_wide() + truth_vector();
    printf("%d %d\n", checked, local[0]);
    return 0;
}
