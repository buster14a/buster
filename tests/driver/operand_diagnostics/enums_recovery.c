// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Type errors precede errors introduced by an enumerator's recovered value or name.

typedef _Atomic(int) AtomicInt;
typedef float FloatAlias;
const FloatAlias floating_value = 1.5;

enum QualifiedValue
{
    QualifiedRejected = floating_value,
    QualifiedAfter,
};
_Static_assert(QualifiedRejected == 0 && QualifiedAfter == 1, "qualified value recovery");

enum OverflowRecovery : unsigned char
{
    OverflowSeed = 255,
    OverflowRejected = (AtomicInt)1,
    OverflowAfter,
};
_Static_assert(OverflowRejected == 0 && OverflowAfter == 1, "overflow recovery");

enum DuplicateRecovery
{
    Duplicate = 1,
    Duplicate = (AtomicInt)2,
    DuplicateAfter,
};
_Static_assert(Duplicate == 1 && DuplicateAfter == 0, "duplicate recovery");

int later_enum_recovery_error = missing_enum_recovery_name;
