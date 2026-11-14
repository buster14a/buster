// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef _Atomic(int) Atomic;
void record(struct S s)
{
    (void)+s;
    (void)-s;
    (void)!s;
    (void)~s;
}
void aliases(Record s, int x)
{
    (void)-s;
    (void)-(_Atomic int)x;
    (void)!(_Atomic int)x;
    (void)~(Atomic)x;
}
void atomic_lvalue(_Atomic(float) x)
{
    (void)~x;
}
