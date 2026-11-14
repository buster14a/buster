// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef struct S OtherRecord;
typedef _Atomic(int) Atomic;
void record(Record s, Record t, OtherRecord u, int x)
{
    (void)(s < x);
    (void)(x <= s);
    (void)(s > t);
    (void)(s > u);
    (void)(s == x);
    (void)(x != s);
    (void)(s && x);
    (void)(x || s);
}
void atomic(int x)
{
    (void)((_Atomic int)x < x);
    (void)(x >= (Atomic)x);
    (void)((Atomic)x == x);
    (void)(x && (Atomic)x);
}
