// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long mine(union U);
unsigned long mine_own(union A);
#define BUSTER_LATE_ALIAS_TEXT_(name) #name
#define BUSTER_LATE_ALIAS_TEXT(name) BUSTER_LATE_ALIAS_TEXT_(name)
static unsigned long never_called(union U) __attribute__((alias(BUSTER_LATE_ALIAS_TEXT(mine))));
unsigned long own_alias(union A) __attribute__((alias(BUSTER_LATE_ALIAS_TEXT(mine_own))));
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_plain(union U);
unsigned long external_late_own(union A);
unsigned long external_late_plain_apply(unsigned long (*)(union U), int);
unsigned long external_late_own_apply(unsigned long (*)(union A), int);
unsigned long mine(union U u) { return external_late_plain(u) + 11; }
unsigned long mine_own(union A u) { return external_late_own(u) + 13; }
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(mine(u.p) + own_alias(a.p) + external_late_plain_apply(mine, n) + external_late_own_apply(own_alias, n));
}
