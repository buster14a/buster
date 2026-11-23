// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef _Atomic(int) AtomicInt;

void f(int condition, int left, int right)
{
    _Atomic __auto_type value = (0, condition) ? (1, (AtomicInt)left) : (2, (AtomicInt)right);
    _Atomic __auto_type grouped = (3, (condition ? (4, (AtomicInt)left) : (5, (AtomicInt)right)));
}
