// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) AtomicInt;
typedef _Atomic(float) AtomicFloat;
typedef _Atomic(int *) AtomicPointer;

void f(int condition, int left, int right, int *before, int *after)
{
    __auto_type integer = condition ? (AtomicInt)left : (AtomicInt)right;
    __auto_type floating = condition ? (AtomicFloat)1.5f : (AtomicFloat)2.5f;
    __auto_type pointer = condition ? (AtomicPointer)before : (AtomicPointer)after;
    _Atomic __auto_type lvalue = condition ? integer : integer;
    _Static_assert(__builtin_types_compatible_p(__typeof__(integer), AtomicInt), "atomic integer deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(floating), AtomicFloat), "atomic floating deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(pointer), AtomicPointer), "atomic pointer deduction");
    _Static_assert(__builtin_types_compatible_p(__typeof__(lvalue), AtomicInt), "atomic lvalue conversion");
    (void)integer;
    (void)floating;
    (void)pointer;
    (void)lvalue;
}
