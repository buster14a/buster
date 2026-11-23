// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

enum LogicalValue
{
    logical_four = 4,
};

const int logical_global_four = 4;

int logical_direct_and(int x)
{
    return x && 4;
}

int logical_direct_or(int x)
{
    return x || 8;
}

int logical_folded(int x)
{
    return (x && (2 + 3)) + (x || -1);
}

int logical_integer_casts(int x)
{
    return (x && (unsigned char)9) + (x || (int)4.0);
}

int logical_enum_expression(int x)
{
    return (x && +logical_four) + (x || (int)logical_four);
}

int logical_parenthesized_enumerator(int x)
{
    return (x && (logical_four)) + ((logical_four) || 8);
}

int logical_character(int x)
{
    return x && 'Q';
}

int logical_type_sizes(int x)
{
    return (x && sizeof(int)) + (x || _Alignof(int));
}

int logical_known_objects(int x)
{
    const unsigned local = 16;
    return (x && logical_global_four) + (x || local);
}

int logical_integer_boolean_results(int x, int y)
{
    return ((x < y) && 4) + ((!x) || 8);
}

int logical_wide(int x)
{
    return x && ((__int128)1 << 100);
}
