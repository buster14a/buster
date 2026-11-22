// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Balanced GNU/C23 suppress recovery (#2332): retained and discarded list siblings, both syntax orders, macro
// boundaries, statement subjects, and later independent errors. Case 47 executes the accepted statement forms.

#if BUSTER_CASE == 47
int main(void)
{
    int value = 1;
    int target = 0;
    int* pointer = &target;
    [[clang::suppress("a")]] __attribute__((suppress("b"))) value++;
    [[clang::suppress("a")]] __attribute__((suppress("b"))) (value)++;
    [[clang::suppress("a")]] __attribute__((suppress("b"))) *pointer = value;
    [[clang::suppress("a")]] __attribute__(()) ++value;
    [[]] __attribute__((suppress("b"))) value += target;
    return value != 7 || target != 3;
}
#else
int f(int);
int suppress_mixed_lists(int value, int* pointer)
{
#if BUSTER_CASE == 0
    [[clang::suppress("a")]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 1
    [[clang::suppress(1)]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 2
    [[clang::suppress("a")]] __attribute__((suppress(1))) value++;
#elif BUSTER_CASE == 3
    [[clang::suppress(1)]] __attribute__((suppress(1))) value++;
#elif BUSTER_CASE == 4
    [[clang::suppress("a" value)]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 5
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 6
    [[clang::suppress("a",), gnu::unused]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 7
    [[gnu::unused, clang::suppress("a" value)]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 8
    [[clang::suppress("a" value)]] __attribute__((unused)) (value)++;
#elif BUSTER_CASE == 9
    [[clang::suppress("a" value)]] __attribute__((suppress("b"))) int kept = 1;
    value += kept;
#elif BUSTER_CASE == 10
    [[maybe_unused]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 11
    [[clang::suppress("a" value)]] __attribute__((suppress(1))) value++;
#elif BUSTER_CASE == 12
    [[clang::suppress("a",)]] __attribute__((suppress(1))) value++;
#elif BUSTER_CASE == 13
    [[clang::suppress("a")]] __attribute__(()) value++;
#elif BUSTER_CASE == 14
    [[]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 15
    [[unknown]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 16
    [[clang::suppress(1)]] __attribute__(()) value++;
#elif BUSTER_CASE == 17
    [[clang::suppress("a" value)]] __attribute__(()) value++;
#elif BUSTER_CASE == 18
    [[unknown]] __attribute__(()) value++;
#elif BUSTER_CASE == 19
    [[gnu::unused]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 20
    [[clang::suppress("a")]] __attribute__((suppress("b"))) *pointer = value;
#elif BUSTER_CASE == 21
    [[clang::suppress("a")]] __attribute__((suppress("b"))) (value)++;
#elif BUSTER_CASE == 22
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) *pointer = value;
#elif BUSTER_CASE == 23
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) (value)++;
#elif BUSTER_CASE == 24
    [[clang::suppress("a")]] __attribute__((suppress("b"))) f(value);
#elif BUSTER_CASE == 25
    [[clang::suppress("a")]] __attribute__((suppress("b"))) label: value++;
#elif BUSTER_CASE == 26
    if (value) [[clang::suppress("a")]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 27
    __attribute__((suppress(1))) [[clang::suppress("a")]] value++;
#elif BUSTER_CASE == 28
    [[clang::suppress("a")]] __attribute__((suppress("b"))) return value;
#elif BUSTER_CASE == 29
    [[clang::suppress("a" value)]] __attribute__((suppress("b"))) return value;
#elif BUSTER_CASE == 30
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) return value;
#elif BUSTER_CASE == 31
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) ;
#elif BUSTER_CASE == 32
    [[clang::suppress(("a" value)), gnu::unused]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 33
#define PREFIX [[clang::suppress("a" value), gnu::unused]]
    PREFIX __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 34
#define ARG "a"[0]
    [[clang::suppress(ARG)]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 35
#define GNU_SUPPRESS(...) __attribute__((suppress(__VA_ARGS__)))
    [[clang::suppress("a")]] GNU_SUPPRESS("b") value++;
#elif BUSTER_CASE == 36
#define BOTH [[clang::suppress(1)]] __attribute__((suppress((2))))
    BOTH value++;
#elif BUSTER_CASE == 37
#define ARG 1
    [[clang::suppress(ARG)]] __attribute__((suppress("b"))) value++;
#elif BUSTER_CASE == 38
#define PREFIX [[clang::minsize]] __attribute__((suppress(1)))
    PREFIX label: value++;
#elif BUSTER_CASE == 39
#define SUFFIX __attribute__((minsize, suppress(1)))
    [[clang::suppress(2)]] SUFFIX label: value++;
#elif BUSTER_CASE == 40
    [[clang::minsize]] __attribute__((suppress(1))) label: value++;
#elif BUSTER_CASE == 41
    [[clang::suppress(1)]] __attribute__((minsize)) label: value++;
#elif BUSTER_CASE == 42
#define BOTH [[clang::suppress("a" value), clang::minsize]] __attribute__((suppress("b")))
    BOTH label: value++;
#elif BUSTER_CASE == 43
    [[clang::suppress("a")]] __attribute__((suppress("b" value))) value++;
#elif BUSTER_CASE == 44
#define BOTH [[clang::suppress("a")]] __attribute__((suppress("b" value)))
    BOTH value++;
#elif BUSTER_CASE == 45
    [[clang::suppress("a")]] __attribute__((suppress("b" value))) int discarded;
    (void)discarded;
#elif BUSTER_CASE == 46
    [[clang::suppress("a")]] __attribute__((suppress("b",), unused)) value++;
#elif BUSTER_CASE == 48
    [[clang::suppress(1)]] __attribute__((suppress(1))) value += undeclared_tail;
#elif BUSTER_CASE == 49
    [[clang::suppress("a" value), gnu::unused]] __attribute__((suppress("b"))) value += undeclared_tail;
#elif BUSTER_CASE == 50
    [[gnu::aligned(3), gnu::unused, clang::suppress("a" value)]] __attribute__((suppress("b"))) value += undeclared_tail;
#elif BUSTER_CASE == 51
    [[clang::suppress("a")]] __attribute__((suppress("b" value))) value += undeclared_tail;
#elif BUSTER_CASE == 52
#undef __clang__
    [[__clang__::suppress("a")]] __attribute__((suppress("b" value))) return value;
#elif BUSTER_CASE == 53
    [[clang::suppress(1)]] __attribute__((suppress(1))) fresh;
    (void)fresh;
#elif BUSTER_CASE == 54
    [[clang::suppress(1)]] __attribute__((suppress(1))) fresh();
    (void)fresh;
#elif BUSTER_CASE == 55
    [[clang::suppress(1)]] __attribute__((suppress(1))) fresh += 1;
    (void)fresh;
#elif BUSTER_CASE == 56
    __attribute__((suppress(1), unused)) [[clang::suppress("a")]] value++;
#elif BUSTER_CASE == 57
    __attribute__(()) [[clang::suppress("a")]] value++;
#elif BUSTER_CASE == 58
    __attribute__((suppress(1))) [[clang::suppress(1)]] value++;
#elif BUSTER_CASE == 59
    __attribute__((suppress(1))) [[clang::suppress("a")]] *pointer = value;
#elif BUSTER_CASE == 60
    __attribute__((suppress(1))) [[clang::suppress("a")]] int kept = 1;
    value += kept;
#elif BUSTER_CASE == 61
    [[__msvc__::noinline]] __attribute__((suppress("b" value))) return value;
#elif BUSTER_CASE == 62
    [[msvc::__noinline__]] __attribute__((suppress("b" value))) return value;
#elif BUSTER_CASE == 63
    __attribute__((suppress("a"))) [[clang::suppress(1)]] value++;
#endif
    return undeclared_after;
}
#endif
