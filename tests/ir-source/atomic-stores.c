// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1396: seq_cst stores stay write-only in the typed stream and IR, and become memory xchg in both native tiers.
// Initializing stores, desired values kept live, nested assignments, exactly-once operands, and raw float/bool/pointer
// results run through both interpreters and both Tier 1 paths against clang. The interpreter unit tests separately
// require initializing stores to pass with undefined-byte checking enabled (the source runner permits native writes).
#include <stdio.h>

static unsigned stored(int value)
{
    signed char byte;
    short half;
    int word;
    long wide;
    int* pointer;
    __atomic_store_n(&byte, (signed char)value, __ATOMIC_SEQ_CST);
    __atomic_store_n(&half, (short)(value * 3), __ATOMIC_SEQ_CST);
    __atomic_store_n(&word, value * 5, __ATOMIC_SEQ_CST);
    __atomic_store_n(&wide, (long)value * 100000003L, __ATOMIC_SEQ_CST);
    __atomic_store_n(&pointer, &word, __ATOMIC_SEQ_CST);
    unsigned wrong = byte != (signed char)value || half != (short)(value * 3) || word != value * 5 || wide != (long)value * 100000003L || pointer != &word;

    _Atomic int first;
    _Atomic int second;
    _Atomic int targets[2];
    int inputs[2] = { value + 11, value + 17 };
    int address_calls = 0;
    int value_calls = 0;
    int desired = (first = (second = (targets[address_calls++] = inputs[value_calls++])));
    wrong += desired != value + 11 || first != desired || second != desired || targets[0] != desired || address_calls != 1 || value_calls != 1;
    __c11_atomic_store(&targets[1], value + 29, __ATOMIC_SEQ_CST);
    wrong += __c11_atomic_load(&targets[1], __ATOMIC_RELAXED) != value + 29;
    // The converted value, including a negative signed byte, is also the assignment's result.
    _Atomic signed char narrow;
    signed char converted = (narrow = value);
    wrong += converted != (signed char)value || narrow != converted;
    _Atomic _Bool flag;
    _Bool truth = (flag = value);
    wrong += truth != (value != 0) || flag != truth;
    _Atomic(int*) atomic_pointer;
    int* same = (atomic_pointer = &word);
    wrong += same != &word || atomic_pointer != same || *same != value * 5;

    _Atomic float single;
    _Atomic double twice;
    union { float value; unsigned bits; } single_wanted, single_result, single_stored;
    union { double value; unsigned long bits; } twice_wanted, twice_result, twice_stored;
    single_wanted.value = value == 0 ? -0.0f : (float)value / 4.0f;
    twice_wanted.value = value == 0 ? -0.0 : (double)value / 8.0;
    single_result.value = (single = single_wanted.value);
    twice_result.value = (twice = twice_wanted.value);
    single_stored.value = single;
    twice_stored.value = twice;
    wrong += single_result.bits != single_wanted.bits || single_stored.bits != single_wanted.bits;
    wrong += twice_result.bits != twice_wanted.bits || twice_stored.bits != twice_wanted.bits;
    return wrong;
}

int main(void)
{
    unsigned wrong = 0;
    for (int value = -129; value <= 129; value += 43)
    {
        wrong += stored(value);
    }
    printf("%u\n", wrong);
    return wrong != 0;
}
