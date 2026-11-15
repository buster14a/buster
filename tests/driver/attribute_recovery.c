// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #2332 case 3. Each malformed list must retain the independent error after it.
// Select a case at runtime through the real driver's -D option; this program is never included by a unity unit.

int attribute_recovery(int value, int* pointer)
{
#if BUSTER_CASE == 0
    __attribute__((suppress("first",))) value++;
#elif BUSTER_CASE == 1
    __attribute__((suppress(("first") value))) value++;
#elif BUSTER_CASE == 2
    __attribute__((suppress("first" value))) value++;
#elif BUSTER_CASE == 3
    [[clang::suppress("first",)]] value++;
#elif BUSTER_CASE == 4
    [[clang::suppress(("first") value)]] value++;
#elif BUSTER_CASE == 5
    [[clang::suppress("first" value)]] value++;
#elif BUSTER_CASE == 6
    __attribute__((suppress((("first" value) value)))) value++;
#elif BUSTER_CASE == 7
    __attribute__((suppress(()))) value++;
#elif BUSTER_CASE == 8
    __attribute__((suppress((())))) value++;
#elif BUSTER_CASE == 9
    __attribute__((suppress("first"))) (value)++;
    __attribute__((suppress(("second")))) *pointer = 1;
    return value;
#elif BUSTER_CASE == 10
    __attribute__((suppress("first" value), suppress("second"))) value++;
#elif BUSTER_CASE == 11
    __attribute__((suppress("first",), suppress((1)))) value++;
#elif BUSTER_CASE == 12
    __attribute__((suppress("first",))) label: value++;
#elif BUSTER_CASE == 13
label: __attribute__((suppress("first" value))) value++;
#elif BUSTER_CASE == 14
    __attribute__((suppress("first" value))) int discarded;
    (void)discarded;
#elif BUSTER_CASE == 15
    [[clang::suppress("first" value)]] int retained = 0;
    (void)retained;
#elif BUSTER_CASE == 16
    __attribute__((suppress("first" value))) int discarded = (1 + 2);
    {
        int retained = undeclared_block;
        (void)retained;
    }
#elif BUSTER_CASE == 17
    __attribute__((suppress("first"[0]))) value++;
#elif BUSTER_CASE == 18
    [[clang::suppress("first"[0])]] value++;
#elif BUSTER_CASE == 19
    __attribute__((suppress(("first"[0])))) value++;
#elif BUSTER_CASE == 20
    [[clang::suppress(("first"[0]))]] value++;
#elif BUSTER_CASE == 21
    __attribute__((suppress(("first")[0]))) value++;
#elif BUSTER_CASE == 22
    [[clang::suppress(("first")[0])]] value++;
#elif BUSTER_CASE == 23
    __attribute__((suppress("first"[0][1]))) value++;
#elif BUSTER_CASE == 24
    [[clang::suppress("first"[0][1])]] value++;
#elif BUSTER_CASE == 25
    __attribute__((suppress(("first"[undeclared_index(0)])))) value++;
#elif BUSTER_CASE == 26
#define BUSTER_SUPPRESS_ARG ("first"[0])
    __attribute__((suppress(BUSTER_SUPPRESS_ARG))) value++;
#elif BUSTER_CASE == 27
    __attribute__((suppress("first"[0]), suppress("second"))) value++;
#elif BUSTER_CASE == 28
    __attribute__((suppress(("first"[0])))) __attribute__((suppress("second"))) value++;
#elif BUSTER_CASE == 29
    __attribute__((suppress("first"[0]))) label: value++;
#elif BUSTER_CASE == 30
label: __attribute__((suppress("first"[0]))) value++;
#elif BUSTER_CASE == 31
    if (value) __attribute__((suppress("first"[0]))) value++;
#elif BUSTER_CASE == 32
    __attribute__((suppress("first"[0]))) int discarded;
    (void)discarded;
#elif BUSTER_CASE == 33
    [[clang::suppress("first"[0])]] int retained = 0;
    (void)retained;
#elif BUSTER_CASE == 34
    __attribute__((suppress((("first"[0]))), suppress(("second"[1])))) value++;
#elif BUSTER_CASE == 35
    __attribute__((suppress(("first" "second"), (("third"))))) value++;
    [[clang::suppress(("fourth" "fifth"))]] *pointer = 1;
    __attribute__((assume(pointer[0] == value))) ;
    return value;
#endif
#if BUSTER_CASE != 9 && BUSTER_CASE != 35
    return undeclared_after;
#endif
}
