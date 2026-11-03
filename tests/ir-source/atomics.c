// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's atomic loads and stores in the IR: every width and order, GNU's _n and C11's, a pointer's, and an implicit
// _Atomic object's, each aligned and of a constant order and so taken directly (the typed stream's marked LOAD and SET,
// and a seq_cst store's FENCE; the IR's atomic load and store, and its fence) rather than by a helper's call, which the
// IR's interpreter has no code for. Its read/modify/writes likewise: exchanges and additions returning the old value or
// the new one, GNU's, __sync's and C11's, a test-and-set, a pointer's exchange, and an implicit _Atomic object's
// increments, decrements and compound additions (the typed stream's ATOMIC_RMW; the IR's rmw); and compare-exchanges
// that succeed and that fail, GNU's, __sync's (bool and val), C11's, the generic one, and an implicit _Atomic object's
// compound multiplication, exclusive or and shift, their loops' (the typed stream's ATOMIC_CAS; the IR's rmw cas).
// Promotion leaves every atomic access in memory. test-ir-source lowers them by both
// paths, promotes them and reads their text back; test-ssa-source runs the program in the IR's three forms against
// clang -O0's build's output.
#include <stdio.h>

static _Atomic int counter;
static _Atomic char flag;
static long wide;
static short narrow;
static int* pointer;
static unsigned char byte;

static int published(int value)
{
    __atomic_store_n(&wide, (long)value << 33, __ATOMIC_SEQ_CST);
    __atomic_store_n(&narrow, (short)value, __ATOMIC_RELEASE);
    flag = 1;
    return (int)(__atomic_load_n(&wide, __ATOMIC_ACQUIRE) >> 33) + __atomic_load_n(&narrow, __ATOMIC_RELAXED) + flag;
}

static long updated(int value, int* local)
{
    long sum = __atomic_exchange_n(&wide, (long)value << 2, __ATOMIC_ACQ_REL);
    sum += __atomic_fetch_add(&wide, 5, __ATOMIC_RELAXED);
    sum += __atomic_sub_fetch(&narrow, (short)2, __ATOMIC_SEQ_CST);
    sum += __atomic_add_fetch(&narrow, (short)-7, __ATOMIC_RELEASE);
    sum += __sync_fetch_and_add(&wide, 3);
    sum += __sync_sub_and_fetch(&byte, 1);
    sum += __sync_lock_test_and_set(&byte, (unsigned char)(value + 1));
    sum += __atomic_test_and_set(&byte, __ATOMIC_ACQUIRE);
    sum += *__atomic_exchange_n(&pointer, local, __ATOMIC_SEQ_CST);
    sum += __c11_atomic_fetch_sub(&counter, 4, __ATOMIC_RELEASE);
    sum += __c11_atomic_exchange(&flag, 2, __ATOMIC_CONSUME);
    sum += __c11_atomic_fetch_add(&flag, -3, __ATOMIC_ACQUIRE);
    counter += value;
    sum += counter++;
    sum += --flag;
    sum += counter -= 9;
    return sum + __atomic_load_n(&wide, __ATOMIC_SEQ_CST) + counter + flag;
}

static long exchanged(int value)
{
    long expected = __atomic_load_n(&wide, __ATOMIC_RELAXED);
    long sum = __atomic_compare_exchange_n(&wide, &expected, expected + value, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
    sum = sum * 3 + expected;
    expected = wide + 1;
    sum = sum * 3 + __atomic_compare_exchange_n(&wide, &expected, 7, 1, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    sum = sum * 3 + expected + wide;
    short old = narrow;
    sum = sum * 3 + __sync_bool_compare_and_swap(&narrow, old, (short)(old * 5));
    sum = sum * 3 + __sync_bool_compare_and_swap(&narrow, old, (short)9);
    sum = sum * 3 + __sync_val_compare_and_swap(&byte, byte, (unsigned char)(value + 200));
    sum = sum * 3 + __sync_val_compare_and_swap(&byte, (unsigned char)(byte + 1), (unsigned char)3);
    int current = counter;
    sum = sum * 3 + __c11_atomic_compare_exchange_strong(&counter, &current, value * 11, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    current = -1;
    sum = sum * 3 + __c11_atomic_compare_exchange_weak(&counter, &current, 4, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    sum = sum * 3 + current;
    unsigned char desired = (unsigned char)(value + 1);
    unsigned char seen = byte;
    sum = sum * 3 + __atomic_compare_exchange(&byte, &seen, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    seen = (unsigned char)(seen + 1);
    sum = sum * 3 + __atomic_compare_exchange(&byte, &seen, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    sum = sum * 3 + seen;
    sum = sum * 3 + (counter *= 3);
    sum = sum * 3 + (flag ^= (char)value);
    sum = sum * 3 + (counter <<= 2);
    return sum + wide + narrow + byte + counter + flag;
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
    total += (int)updated(total, &local);
    total += (int)exchanged(total);
    printf("%d\n", total);
    return 0;
}
