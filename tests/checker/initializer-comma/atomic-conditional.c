// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) AtomicInt;
typedef _Atomic(int) OtherInt;
typedef _Atomic(float) AtomicFloat;
typedef _Atomic(int *) AtomicPointer;
typedef int AlignedInt __attribute__((aligned(16)));
typedef _Atomic(AlignedInt) AlignedAtomic;

void integers(int condition, int left, int right)
{
    _Atomic __auto_type raw = condition ? (_Atomic int)left : (_Atomic int)right;
    _Atomic __auto_type same = condition ? (AtomicInt)left : (AtomicInt)right;
    _Atomic __auto_type different = condition ? (AtomicInt)left : (OtherInt)right;
    const volatile _Atomic __auto_type qualified = condition ? (_Atomic int)left : (_Atomic int)right;
    _Atomic __auto_type nested = condition ? (left ? (AtomicInt)left : (AtomicInt)right) : (AtomicInt)right;
    _Atomic __auto_type aligned = condition ? (AlignedAtomic)left : (AtomicInt)right;
}

void floating(int condition, float left, float right)
{
    _Atomic __auto_type value = condition ? (AtomicFloat)left : (AtomicFloat)right;
}

void pointers(int condition, int *left, int *right)
{
    _Atomic __auto_type value = condition ? (AtomicPointer)left : (AtomicPointer)right;
}

_Atomic __auto_type constant = 1 ? (_Atomic int)3 : (_Atomic int)4;
