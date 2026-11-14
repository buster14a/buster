// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Rejected enumerator types preserve their spelling and recover as implicit successors.

typedef _Atomic(int) AtomicInt;
typedef _Atomic(float) AtomicFloat;
typedef float FloatAlias;
typedef int* IntPointer;

enum Recovery
{
    Seed = 5,
    BadAtomic = (AtomicInt)1,
    AfterAtomic,
    BadRawAtomic = (_Atomic(int))2,
    AfterRawAtomic,
    BadFloat = 1.5,
    AfterFloat,
    BadFloatAlias = (FloatAlias)2.5,
    AfterFloatAlias,
    BadAtomicFloat = (AtomicFloat)3.5,
    AfterAtomicFloat,
    BadPointer = (IntPointer)0,
    AfterPointer,
    BadGrouped = (((AtomicInt)3)),
    AfterGrouped,
    BadGeneric = _Generic(0, int: (AtomicInt)4),
    AfterGeneric,
};

_Static_assert(BadAtomic == 6 && AfterAtomic == 7, "atomic successor");
_Static_assert(BadRawAtomic == 8 && AfterRawAtomic == 9, "raw atomic successor");
_Static_assert(BadFloat == 10 && AfterFloat == 11, "floating successor");
_Static_assert(BadFloatAlias == 12 && AfterFloatAlias == 13, "floating typedef successor");
_Static_assert(BadAtomicFloat == 14 && AfterAtomicFloat == 15, "atomic floating successor");
_Static_assert(BadPointer == 16 && AfterPointer == 17, "pointer successor");
_Static_assert(BadGrouped == 18 && AfterGrouped == 19, "grouped successor");
_Static_assert(BadGeneric == 20 && AfterGeneric == 21, "generic successor");

int enum_function(void);
enum MoreRejectedTypes
{
    BadAtomicDivision = (AtomicInt)(1 / 0),
    BadFloatingDivision = 1.0 / 0.0,
    BadComma = (0, (AtomicInt)1),
    BadFunction = enum_function,
};

enum FixedRecovery : unsigned char
{
    FixedSeed = 5,
    FixedRejected = (AtomicInt)1,
    FixedAfter,
};
_Static_assert(FixedRejected == 6 && FixedAfter == 7, "fixed underlying successor");

int later_enum_error = missing_enum_name;
