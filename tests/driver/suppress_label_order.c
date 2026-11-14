// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #2332 mixed-label-attribute order cases. Read at runtime, never included in a unity unit.

int suppress_label_order(int value)
{
#if BUSTER_CASE == 0
    __attribute__((suppress(1), minsize)) label: value++;
#elif BUSTER_CASE == 1
    __attribute__((minsize, suppress(1))) label: value++;
#elif BUSTER_CASE == 2
label: __attribute__((suppress(1), minsize)) value++;
#elif BUSTER_CASE == 3
label: __attribute__((minsize, suppress(1))) value++;
#elif BUSTER_CASE == 4
    __attribute__((suppress((1)), minsize)) label: value++;
#elif BUSTER_CASE == 5
    __attribute__((minsize, suppress((1)))) label: value++;
#elif BUSTER_CASE == 6
    __attribute__((suppress(1))) __attribute__((minsize)) label: value++;
#elif BUSTER_CASE == 7
    __attribute__((minsize)) __attribute__((suppress(1))) label: value++;
#elif BUSTER_CASE == 8
    [[clang::suppress(1), clang::minsize]] label: value++;
#elif BUSTER_CASE == 9
    [[clang::minsize, clang::suppress(1)]] label: value++;
#elif BUSTER_CASE == 10
    __attribute__((suppress(1), suppress(2), minsize)) label: value++;
#elif BUSTER_CASE == 11
    __attribute__((suppress("first"), minsize)) label: value++;
#elif BUSTER_CASE == 12
    __attribute__((minsize, suppress("first"))) label: value++;
#elif BUSTER_CASE == 13
    __attribute__((suppress(1), unused)) label: value++;
#elif BUSTER_CASE == 14
    __attribute__((unused, __unused__)) label: value++;
#elif BUSTER_CASE == 15
    __attribute__((__suppress__(1), __minsize__)) label: value++;
#elif BUSTER_CASE == 16
#define BUSTER_SUPPRESS __attribute__((suppress(1)))
#define BUSTER_MINSIZE __attribute__((minsize))
    BUSTER_SUPPRESS BUSTER_MINSIZE label: value++;
#elif BUSTER_CASE == 17
#define BUSTER_SUPPRESS __attribute__((suppress((1))))
#define BUSTER_MINSIZE __attribute__((minsize))
    BUSTER_MINSIZE BUSTER_SUPPRESS label: value++;
#elif BUSTER_CASE == 18
    [[maybe_unused, gnu::unused]] label: value++;
#elif BUSTER_CASE == 19
    __attribute__((unused)) __attribute__((__unused__)) label: value++;
#endif
    return value;
}

#if BUSTER_CASE != 14 && BUSTER_CASE != 18 && BUSTER_CASE != 19
int suppress_label_order_after(void)
{
    return undeclared_after;
}
#endif
