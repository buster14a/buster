// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
typedef struct UnitPair UnitPair;
struct UnitPair
{
    long first;
    long second;
};
extern long shared;
extern UnitPair (*operation)(UnitPair);
int selected(void);
