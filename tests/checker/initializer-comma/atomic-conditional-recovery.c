// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) AtomicInt;

int f(int condition, int left, int right)
{
    _Atomic __auto_type value = condition ? (AtomicInt)left : (AtomicInt)right;
    int *pointer = &value;
    int later = missing;
    return value + *pointer + later;
}
