// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original malformed-delimiter regressions for #2332. GNU recovery keeps a semicolon and later statements;
// a missing C23 list bracket may consume the remaining body. Both must retain a later independent declaration.
// Selected by the real driver's -D option; runtime fixture data, never included by a unity unit.

int suppress_delimiters(int value)
{
#if BUSTER_CASE == 0
    __attribute__((suppress("a")) value++;
#elif BUSTER_CASE == 1
    __attribute__((suppress("a") value++;
#elif BUSTER_CASE == 2
    __attribute__((suppress("a" value++;
#elif BUSTER_CASE == 3
    [[clang::suppress("a")] value++;
#elif BUSTER_CASE == 4
    [[clang::suppress("a") value++;
#elif BUSTER_CASE == 5
    [[clang::suppress("a"]] value++;
#elif BUSTER_CASE == 6
    __attribute__((suppress("a",)) value++;
#elif BUSTER_CASE == 7
    __attribute__((suppress(1)) value++;
#elif BUSTER_CASE == 8
    [[clang::suppress(1]] value++;
#elif BUSTER_CASE == 9
    __attribute__((suppress("a")))) value++;
#elif BUSTER_CASE == 10
    [[clang::suppress("a")]]] value++;
#elif BUSTER_CASE == 11
    __attribute__((suppress("a"]))) value++;
#elif BUSTER_CASE == 12
    [[clang::suppress("a"})]] value++;
#elif BUSTER_CASE == 13
    __attribute__((suppress("a";))) value++;
#elif BUSTER_CASE == 14
    [[clang::suppress("a";)]] value++;
#elif BUSTER_CASE == 15
    __attribute__((suppress(("a"))) value++;
#elif BUSTER_CASE == 16
    [[clang::suppress(("a")]] value++;
#elif BUSTER_CASE == 17
    __attribute__((suppress((1))) value++;
#elif BUSTER_CASE == 18
    [[clang::suppress((1)]] value++;
#elif BUSTER_CASE == 19
    __attribute__((suppress("a")), unused value++;
#elif BUSTER_CASE == 20
    [[clang::suppress("a"), gnu::unused] value++;
#elif BUSTER_CASE == 21
    __attribute__((suppress("a")) int retained = 0;
    value += retained;
#elif BUSTER_CASE == 22
    [[clang::suppress("a")] int retained = 0;
    value += retained;
#elif BUSTER_CASE == 23
    __attribute__((suppress("a",)) int retained = 0;
    value += retained;
#elif BUSTER_CASE == 24
    [[clang::suppress("a",)] int retained = 0;
    value += retained;
#elif BUSTER_CASE == 25
    __attribute__((unused, suppress("a")) value++;
#elif BUSTER_CASE == 26
    __attribute__((suppress("a"), unused) value++;
#elif BUSTER_CASE == 27
    __attribute__((suppress("a")) __attribute__((suppress((1)))) value++;
#elif BUSTER_CASE == 28
    __attribute__((suppress("a")) [[clang::suppress((1))]] value++;
#elif BUSTER_CASE == 29
    [[clang::suppress("a"]] int retained = 0;
    value += retained;
#elif BUSTER_CASE == 30
    [[clang::suppress(1]] int retained = 0;
    value += retained;
#elif BUSTER_CASE == 31
    int retained __attribute__((suppress("a")) = undeclared_initializer;
    value += retained;
#elif BUSTER_CASE == 32
    int retained [[clang::suppress("a")] = undeclared_initializer;
    value += retained;
#elif BUSTER_CASE == 33
    __attribute__((suppress("a")) ;
#elif BUSTER_CASE == 34
    __attribute__((unused, suppress("a")) ;
#elif BUSTER_CASE == 35
    __attribute__((suppress("a"), aligned(3)) value++;
#elif BUSTER_CASE == 36
    __attribute__((suppress("a"), aligned(1, 2)) value++;
#elif BUSTER_CASE == 37
    __attribute__((suppress("a")) undeclared_in_discard;
#elif BUSTER_CASE == 38
    __attribute__((suppress("a")) value += undeclared_in_discard;
#elif BUSTER_CASE == 39
    if (value) __attribute__((suppress("a")) value++;
#elif BUSTER_CASE == 40
    if (value) __attribute__((suppress("a")) { value++; }
#elif BUSTER_CASE == 41
    [[clang::suppress("a")] value++;
    [[clang::suppress("b")]] value++;
#elif BUSTER_CASE == 42
    [[clang::suppress("a")] value++;
    value = (int[]){1}[0];
#elif BUSTER_CASE == 43
    [[clang::suppress("a")] value++;
    if (value) { value++; }
#elif BUSTER_CASE == 44
    [[clang::suppress(]] value++;
#elif BUSTER_CASE == 45
    [[clang::suppress("a",]] value++;
#elif BUSTER_CASE == 46
    [[clang::suppress("")]] value++;
#elif BUSTER_CASE == 47
    [[clang::suppress(L"a"]] value++;
#elif BUSTER_CASE == 48
    [[clang::suppress(u8"a"]] value++;
#elif BUSTER_CASE == 49
    [[clang::suppress("\x80"]] value++;
#elif BUSTER_CASE == 50
    __attribute__((suppress("a")) __attribute__((aligned(1, 2))) value++;
#elif BUSTER_CASE == 51
    __attribute__((suppress("a")) value = (int[]){1}[0];
#elif BUSTER_CASE == 52
    __attribute__((suppress("a")) value = (value + 1);
#elif BUSTER_CASE == 53
    __attribute__((suppress("a")) value = (value + 1));
#elif BUSTER_CASE == 54
    __attribute__((suppress("a", ("b")))) value++;
#elif BUSTER_CASE == 55
    [[clang::suppress("a", ("b"))]] value++;
#elif BUSTER_CASE == 56
    [[clang::suppress()]] value++;
#elif BUSTER_CASE == 57
    __attribute__((aligned(3))) ;
#elif BUSTER_CASE == 58
    __attribute__((aligned(1, 2))) ;
#endif
#if BUSTER_CASE < 54 || BUSTER_CASE == 57 || BUSTER_CASE == 58
    return undeclared_after;
#else
    return value;
#endif
}
int delimiter_later_function(void)
{
#if BUSTER_CASE < 54 || BUSTER_CASE == 57 || BUSTER_CASE == 58
    return undeclared_global;
#else
    return 0;
#endif
}

#if BUSTER_CASE == 59
// Flow warnings before the parse error remain; only later analysis warnings stop. Runtime warnings still apply.
int delimiter_flow_before(void) { }
int delimiter_flow_recovered(int value)
{
    [[clang::suppress("a")] value++;
    return undeclared_after;
}
int delimiter_flow_after(void) { }
int delimiter_flow_runtime(void) { return 1 / 0; }
int delimiter_flow_later(void) { return undeclared_global; }
#endif
