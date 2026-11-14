// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) Atomic;
typedef _Atomic(int) OtherAtomic;
typedef _Atomic(float) AtomicFloat;
typedef _Atomic(int *) AtomicPointer;
typedef int AlignedInt __attribute__((aligned(16)));
typedef _Atomic(AlignedInt) AlignedAtomic;
_Static_assert(__builtin_types_compatible_p(__typeof__(1 ? (Atomic)1 : (OtherAtomic)2), Atomic), "atomic conditional type");
_Static_assert(__builtin_types_compatible_p(__typeof__(1 ? (AtomicFloat)1 : (AtomicFloat)2), AtomicFloat), "atomic float conditional type");
_Static_assert(__builtin_types_compatible_p(__typeof__(1 ? (AtomicPointer)0 : (AtomicPointer)0), AtomicPointer), "atomic pointer conditional type");
Atomic choose_int(int condition, int left, int right)
{
    return condition ? (Atomic)left : (OtherAtomic)right;
}
AtomicFloat choose_float(int condition, float left, float right)
{
    return condition ? (AtomicFloat)left : (AtomicFloat)right;
}
AtomicPointer choose_pointer(int condition, int *left, int *right)
{
    return condition ? (AtomicPointer)left : (AtomicPointer)right;
}
AlignedAtomic choose_aligned(int condition, int left, int right)
{
    return condition ? (AlignedAtomic)left : (Atomic)right;
}
int main(void)
{
    Atomic first = choose_int(1, 17, 29);
    Atomic second = choose_int(0, 17, 29);
    Atomic folded = 1 ? (Atomic)31 : (OtherAtomic)37;
    AtomicFloat positive = choose_float(1, 1.5f, -2.5f);
    AtomicFloat negative = choose_float(0, 1.5f, -2.5f);
    int left = 3;
    int right = 5;
    AtomicPointer before = choose_pointer(1, &left, &right);
    AtomicPointer after = choose_pointer(0, &left, &right);
    AlignedAtomic aligned_before = choose_aligned(1, 41, 43);
    AlignedAtomic aligned_after = choose_aligned(0, 41, 43);
    return first != 17 || second != 29 || folded != 31 || positive != 1.5f || negative != -2.5f || before != &left || after != &right ||
           aligned_before != 41 || aligned_after != 43;
}
