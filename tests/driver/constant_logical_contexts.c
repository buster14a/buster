// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int logical_unevaluated_size(int x)
{
    return sizeof(x && 4);
}

int logical_unevaluated_type(int x)
{
    __typeof__(x || 8) value = 0;
    return value;
}

int logical_generic_control(int x)
{
    return _Generic(x && 4, int: 1, default: 0);
}

int logical_generic_arms(int x)
{
    return _Generic(x, int: x && 4, default: x || 8);
}

int logical_chosen_arms(int x)
{
    return __builtin_choose_expr(1, x && 4, x || 8);
}

int logical_constant_query(int x)
{
    return __builtin_constant_p(x && 16);
}

int logical_invalid_constant_query(int x)
{
    return __builtin_constant_p(x && (int)1e99) + __builtin_constant_p(x || (int)__builtin_inf()) +
           __builtin_constant_p(x && (int)__builtin_nan(""));
}

int logical_unreachable(int x)
{
    if (0)
    {
        return x && 32;
    }
    return x;
}

_Static_assert(sizeof(0 && 4) > 0, "unevaluated passing assertion");
_Static_assert(__builtin_choose_expr(1, 0 && 4, 1 || 8) == 0, "chosen condition passes");
_Static_assert(0, "failure after chosen condition");
