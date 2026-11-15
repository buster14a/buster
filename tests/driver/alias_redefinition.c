// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#ifndef BUSTER_CASE
#define BUSTER_CASE 0
#endif

int target(void)
{
    return 7;
}

#if BUSTER_CASE == 0
int repeated(void) __attribute__((alias("target")));
int repeated(void) __attribute__((alias("target"))) { return 1; }
#elif BUSTER_CASE == 1
int repeated(void) __attribute__((alias("target")));
__attribute__((alias("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 2
int repeated(void) { return 2; }
__attribute__((alias("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 3
int repeated(void) __attribute__((alias("target")));
int repeated(void);
__attribute__((alias("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 4
int (*resolve(void))(void) { return target; }
int repeated(void) __attribute__((ifunc("resolve")));
int repeated(void) __attribute__((ifunc("resolve"))) { return 1; }
#elif BUSTER_CASE == 5
static int repeated(void) __attribute__((weakref("target")));
static __attribute__((weakref("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 6
int repeated(void) __attribute__((alias("target")));
__attribute__((alias("target"), alias("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 7
int repeated(void) __attribute__((alias("target")));
__attribute__((alias("target"), aligned(16))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 8
int repeated(void) __attribute__((alias("target")));
__attribute__((alias("target"))) long repeated(void) { return 1; }
#elif BUSTER_CASE == 9
int repeated(void) __attribute__((alias("target")));
int repeated(void);
#elif BUSTER_CASE == 10
int repeated(void) __attribute__((alias("target")));
int repeated(void) { return 1; }
#elif BUSTER_CASE == 11
int repeated(void);
__attribute__((alias("target"))) int repeated(void) { return 1; }
#elif BUSTER_CASE == 12
int repeated(void) __attribute__((alias("target")));
[[gnu::alias("target")]] int repeated(void) { return 1; }
#elif BUSTER_CASE == 13
#define ALIAS_TARGET __attribute__((alias("target")))
int repeated(void) ALIAS_TARGET;
ALIAS_TARGET int repeated(void) { return 1; }
#elif BUSTER_CASE == 14
int repeated(void) __attribute__((alias("target")));
int repeated(void) __attribute__((aligned(32), alias("target"))) { return 1; }
#endif

int use(void)
{
    return repeated();
}

#if BUSTER_CASE != 9
int after = undeclared_after;
#endif
