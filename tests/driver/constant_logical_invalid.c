// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int logical_invalid_add(int x)
{
    return x && (2147483647 + 1);
}

int logical_invalid_subtract(int x)
{
    return x || (-2147483647 - 2);
}

int logical_invalid_shift_count(int x)
{
    return x && (1 << 32);
}

int logical_invalid_negative_count(int x)
{
    return x || (1 << -1);
}

int logical_invalid_negative_value(int x)
{
    return x && (-1 << 1);
}

int logical_invalid_division(int x)
{
    return (x && ((-2147483647 - 1) / -1)) + (x || (1 / 0));
}

int logical_invalid_floating_sum(int x)
{
    return x && ((int)1e99 + 4);
}

int logical_invalid_floating_comma(int x)
{
    return x || ((int)1e99, 4);
}

int logical_invalid_floating_condition(int x)
{
    return x && ((int)1e99 ? 4 : 5);
}

int logical_invalid_query_add(int x)
{
    return __builtin_constant_p(x && (2147483647 + 1));
}

int logical_invalid_query_shift(int x)
{
    return __builtin_constant_p(x || (1 << 32));
}

int logical_invalid_query_division(int x)
{
    return __builtin_constant_p(x && ((-2147483647 - 1) / -1));
}

int logical_invalid_query_floating(int x)
{
    return __builtin_constant_p(x || ((int)1e99 + 4));
}

int logical_invalid_builtin_abs(int x)
{
    return x && __builtin_abs(2147483647 + 2);
}

int logical_invalid_bit_cast(int x)
{
    return x || __builtin_bit_cast(unsigned int, 2147483647 + 1);
}

int logical_valid_signed_shift(int x)
{
    return x && (1 << 31);
}
