// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3310: every load and store of a volatile lvalue reaches the IR as a volatile access (BUSTER_IR_VOLATILE), which no
// pass removes, merges or forwards: a global's, one through a pointer, an element's, a member's, a cast's (the
// memory-mapped register idiom), a compound assignment's and an increment's read and write, each floating class's and a
// 128-bit integer's, and a volatile bit-field's unit reads and writes (a packed one's nine bytes too). Tier 1 with value
// numbering with memory kept a global's second read as its first's value and a store's value as the next read's. An
// empty asm statement's "memory" clobber, the compiler barrier (with an operand too), which the lowering dropped, is a
// signal fence: the reads of a plain global on either side of it stay two. #3329: an aggregate's copies, read and
// assigned, are volatile pieces too (volatile-vectors.c has the vectors', which the typed stream's interpreter doesn't run);
// so are a compound assignment's read and write of a volatile scalar computed in a complex type, a __float128 or a _BitInt
// past 128 bits (each a copy into a frame object and back), a volatile complex object's parts' (`__real__`, `__imag__`)
// and an empty asm statement's `+` operand's read and write of a volatile object. The program runs in the IR's forms, the
// typed stream's interpreter and Tier 1 against clang; correctness.tests counts the printed volatile accesses and
// fences and each function's loads and stores under --tier1-passes=+all against the default passes'.
#include <stdio.h>

volatile int v;
volatile unsigned long counter;
volatile float vf;
volatile double vd;
volatile long double vl;
volatile __int128 vw;
volatile struct
{
    int a : 5;
    int b : 11;
} vb;
struct __attribute__((packed)) Wide
{
    char pad;
    unsigned long long low : 4;
    // Nine bytes from bit 12: its reads and stores take the unit's first 8 bytes and its ninth.
    unsigned long long bits : 62;
};
volatile struct Wide vwide;
struct Device
{
    int status;
    int data[4];
};
int* volatile vp;
struct Pair
{
    long a;
    long b;
};
volatile struct Pair pair;
volatile double updated_double;
volatile int updated_int;
volatile _Complex double complex_value;
volatile int operand;
int plain;

__attribute__((noinline)) int stored_twice(volatile int* p)
{
    *p = 5;
    *p = 6;
    return *p + *p;
}

__attribute__((noinline)) int read_twice(void)
{
    int a = v;
    int b = v;
    return a + b;
}

__attribute__((noinline)) int discarded(void)
{
    v;
    v;
    return 0;
}

__attribute__((noinline)) int bit_fields(void)
{
    vb.a = 3;
    vb.b = 7;
    return vb.a + vb.b;
}

__attribute__((noinline)) int updated(void)
{
    v += 1;
    v += 1;
    return v;
}

__attribute__((noinline)) int incremented(void)
{
    v++;
    ++v;
    return v;
}

__attribute__((noinline)) int element(volatile int* p)
{
    p[1] = 1;
    p[1] = 2;
    return p[1] + p[1];
}

__attribute__((noinline)) int member(volatile struct Device* device)
{
    device->status = 1;
    device->data[2] = device->status;
    return device->status + device->data[2];
}

__attribute__((noinline)) int mapped(unsigned long address)
{
    *(volatile int*)address = 9;
    return *(volatile int*)address + *(volatile int*)address;
}

__attribute__((noinline)) double floating(void)
{
    vf = 1.5f;
    vd = 2.25;
    vl = 3.0L;
    return (double)(vf + vf) + vd + vd + (double)(vl + vl);
}

__attribute__((noinline)) long wide(void)
{
    vw = (__int128)7 << 64 | 3;
    return (long)(vw >> 64) + (long)vw;
}

__attribute__((noinline)) unsigned long long packed(void)
{
    vwide.bits = 0x123456789abcdefULL;
    return vwide.bits + vwide.bits;
}

__attribute__((noinline)) int pointer(void)
{
    vp = &plain;
    *vp = 4;
    return *vp + plain;
}

__attribute__((noinline)) unsigned long counted(void)
{
    unsigned long a = counter;
    unsigned long b = counter;
    return a - b;
}

__attribute__((noinline)) int barrier(void)
{
    int a = plain;
    __asm__ volatile("" ::: "memory");
    int b = plain;
    return a + b;
}

__attribute__((noinline)) int barrier_operand(int x)
{
    int a = plain;
    __asm__ volatile("" : "+r"(x) : : "memory");
    int b = plain;
    return a + b + x;
}

__attribute__((noinline)) long copied(void)
{
    struct Pair x = pair;
    struct Pair y = pair;
    return x.a + y.b;
}

__attribute__((noinline)) void assigned(struct Pair value)
{
    pair = value;
    pair = value;
}

__attribute__((noinline)) double complex_update(_Complex double z)
{
    updated_double += z;
    updated_double += z;
    return updated_double;
}

__attribute__((noinline)) double quad_update(__float128 q)
{
    updated_double += q;
    updated_double += q;
    return updated_double;
}

__attribute__((noinline)) int limb_update(_BitInt(256) w)
{
    updated_int += w;
    updated_int += w;
    return updated_int;
}

__attribute__((noinline)) double parts(void)
{
    __real__ complex_value = 1.5;
    __imag__ complex_value += 2.0;
    return __real__ complex_value + __real__ complex_value + __imag__ complex_value;
}

__attribute__((noinline)) int asm_operand(void)
{
    __asm__ volatile("" : "+r"(operand));
    __asm__ volatile("" : "+r"(operand));
    return operand;
}

int main(void)
{
    int local = 0;
    struct Device device = { 0, { 0, 0, 0, 0 } };
    int cell[2] = { 0, 0 };
    long checked = stored_twice(&local) + read_twice() + discarded() + bit_fields() + updated() + incremented() + element(cell) +
                   member(&device) + mapped((unsigned long)&cell[0]) + (long)floating() + wide() + (long)(packed() & 0xffff) + pointer() +
                   (long)counted() + barrier() + barrier_operand(5);
    assigned((struct Pair){ 3, 4 });
    checked += copied();
    // quad_update stays uncalled: the IR's interpreters have no libgcc for a __float128's conversion and addition.
    checked += (long)complex_update(1.25) + limb_update(3) + (long)parts() + asm_operand();
    printf("%ld %d %d %d %d %d\n", checked, local, v, cell[0], cell[1], device.data[2]);
    return 0;
}
