// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "units.h"
static long local = 29;
long shared = 41;
static long helper(void)
{
    return local;
}
static UnitPair change(UnitPair value)
{
    value.first += helper();
    value.second += shared;
    shared += 1;
    return value;
}
static UnitPair change_alias(UnitPair) __attribute__((alias("change")));
UnitPair (*operation)(UnitPair) = change_alias;
int selected(void)
{
    return 2;
}
