// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static const int internal = 1;
static const int internal = 1;

int use_internal(void)
{
    return internal;
}

const int external = 1;
const int external = 2;
_Static_assert(external == 1);

static const int chain = 1;
extern const int chain;
static const int chain = 2;
static const int chain = 3;

static const int array[] = { 1, 2 };
static const int array[] = { 3, 4 };
_Static_assert(array[0] == 1);

int nonconstant;
static const int bare = 1;
static const int bare = nonconstant;
