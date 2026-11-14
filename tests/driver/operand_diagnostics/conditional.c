// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef _Atomic(int) Atomic;
void record(Record s, int x)
{
    (void)(s ? 1 : 2);
    (void)(s ?: x);
    (void)(x ? s : 2);
    (void)(x ? 2 : s);
    (void)((_Atomic int)x ? 1 : 2);
    (void)(x ? (Atomic)x : 2);
    (void)(x ? 2 : (_Atomic int)x);
    (void)(x ? (Atomic)x : (_Atomic long)x);
    (void)(x ? (_Atomic long)x : (Atomic)x);
}
void atomic_lvalue(int c, Atomic a, int x)
{
    (void)(c ? a : (Atomic)x);
    (void)(c ? (Atomic)x : a);
}
enum E { E0, E1 };
typedef _Atomic(enum E) AtomicEnum;
typedef _Atomic(unsigned) AtomicUnsigned;
AtomicEnum enum_value(void);
AtomicUnsigned unsigned_value(void);
void atomic_enum(int c)
{
    (void)(c ? enum_value() : unsigned_value());
    (void)(c ? unsigned_value() : enum_value());
}
