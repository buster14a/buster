// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A declaration's retained value and a new invalid arithmetic expression have different constant-evaluation rules.

const int logical_receipt_overflow = 2147483647 + 1;
const int logical_receipt_floating = (int)1e99;
const int logical_receipt_shift = 1 << 32;
const int logical_receipt_division = (-2147483647 - 1) / -1;
const int logical_receipt_value = 4;
const _Complex int logical_receipt_complex = 4 + 2i;
const _Complex double logical_receipt_complex_float = 4.0 + 2.0i;

struct LogicalMixedReceipt
{
    int valid_member;
    int invalid_member;
};

const struct LogicalMixedReceipt logical_mixed_receipt = { 4, 2147483647 + 1 };

int abs(int);

int logical_retained_overflow(int x)
{
    return x && logical_receipt_overflow;
}

int logical_retained_floating(int x)
{
    return x || logical_receipt_floating;
}

int logical_retained_shift(int x)
{
    return x && logical_receipt_shift;
}

int logical_retained_division(int x)
{
    return x || logical_receipt_division;
}

int logical_retained_arithmetic(int x)
{
    return x && (logical_receipt_overflow + logical_receipt_value);
}

int logical_local_receipt_storage(int x)
{
    const int automatic = 2147483647 + 1;
    static const int permanent = 2147483647 + 1;
    return (x && automatic) + (x || permanent);
}

int logical_mixed_receipt_members(int x)
{
    return (x && logical_mixed_receipt.valid_member) + (x || logical_mixed_receipt.invalid_member);
}

int logical_new_invalid_arithmetic(int x)
{
    return (x && (2147483647 + logical_receipt_value)) + (x || (1 << (logical_receipt_value + 32)));
}

int logical_explicit_builtins(int x)
{
    return (x && __builtin_abs(-logical_receipt_value)) + (x || __builtin_bit_cast(unsigned int, logical_receipt_value));
}

int logical_library_call(int x)
{
    return x && abs(logical_receipt_value);
}

int logical_complex_components(int x)
{
    return (x && __real__ logical_receipt_complex) + (x || __imag__ logical_receipt_complex) +
           (x && (int)__real__ logical_receipt_complex_float);
}
