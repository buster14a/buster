// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

constexpr int logical_constexpr_four = 4;

int logical_c23_constants(int x, bool value)
{
    constexpr int local = 8;
    return (x && logical_constexpr_four) + (x || local) + (value && 4) + (x || true) + (x && false);
}

static_assert(true && 4, "boolean left operand stays quiet");
static_assert(1 && 4, "integer left operand warns");
static_assert(0 && 8, "integer failed assertion warns");
