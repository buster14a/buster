// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define LOGICAL_VALUE 4
#define LOGICAL_ID(value) value
#define LOGICAL_FORWARD(value) LOGICAL_ID(value)
#define LOGICAL_AND &&
#define LOGICAL_BODY(value) ((value) && 4)
#define LOGICAL_OR_BODY(value) ((value) || 8)

int logical_macro_values(int x)
{
    return (x && LOGICAL_VALUE) + (LOGICAL_ID(x) || 8) + (x && LOGICAL_ID(4));
}

int logical_macro_only_parentheses(int x)
{
    return LOGICAL_ID((x)) || LOGICAL_FORWARD(8);
}

int logical_macro_operators(int x)
{
    return LOGICAL_BODY(x) + LOGICAL_OR_BODY(x) + (x LOGICAL_AND 4);
}

int logical_macro_arguments(int x)
{
    return LOGICAL_ID(x && 4) + LOGICAL_FORWARD(x || 8);
}

_Static_assert(1 && LOGICAL_VALUE, "macro operand passes");
_Static_assert(LOGICAL_BODY(1), "macro operator is quiet");
