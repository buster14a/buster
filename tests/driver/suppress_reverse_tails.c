// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Failed declarator tails after suppress prefixes retain earlier attribute diagnostics, call-name lookup and later errors.

int suppress_reverse_tails(int value)
{
#if BUSTER_CASE == 0
    __attribute__((suppress(1))) [[clang::suppress("a")]] return value;
#elif BUSTER_CASE == 1
    __attribute__((suppress("a" value))) [[clang::suppress("a")]] value++;
#elif BUSTER_CASE == 2
    __attribute__((suppress(1))) fresh();
    (void)fresh;
#elif BUSTER_CASE == 3
    __attribute__((suppress(1), unused)) fresh();
    (void)fresh;
#endif
    return undeclared_after;
}

int suppress_reverse_tail_later(void)
{
    return undeclared_global;
}
