// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

enum LogicalQuietValue
{
    logical_quiet_four = 4,
};

const volatile int logical_volatile_four = 4;
int logical_mutable_four = 4;
const _Atomic(int) logical_atomic_four = 4;
const _Atomic(_Bool) logical_atomic_true = 1;

int logical_boolean_constants(int x)
{
    return (x && 0) + (x && 1) + (x || 0) + (x || 1);
}

int logical_left_constants(int x)
{
    return (4 && x) + (8 || x);
}

int logical_boolean_type(_Bool b, int x)
{
    return (b && 4) + (b || 8) + (x && (_Bool)4) + (x || (_Bool)8);
}

int logical_noninteger_types(double d, int *p, int x)
{
    return (d && 4) + (p || 8) + (x && (double)4) + (x || (int *)8);
}

int logical_bare_enumerators(int x)
{
    return (x && logical_quiet_four) + (logical_quiet_four || 8);
}

int logical_nonconstant_objects(int x, int y)
{
    const int local = y;
    return (x && local) + (x || logical_volatile_four) + (x && logical_mutable_four);
}

int logical_explicit_boolean_values(int x, int y)
{
    return (x && (y != 0)) + (x || !y) + (x && (2 == 3)) + (x || (2 != 3));
}

int logical_single_bit_casts(int x)
{
    return (x && (unsigned char)257) + (x || (unsigned char)256);
}

int logical_atomic_operands(_Atomic(int) value, _Atomic(_Bool) truth, int x)
{
    return (value && 4) + (truth || 8) + (x && logical_atomic_four) + (x || logical_atomic_true);
}

int logical_invalid_constant_casts(int x)
{
    return (x && (int)1e99) + (x || (int)__builtin_inf()) + (x && (int)__builtin_nan(""));
}
