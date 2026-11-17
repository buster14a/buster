// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Missing suppress delimiters inside, after and between macro expansions. The first diagnostic must materialize
// macro history before choosing its spelling location; repeated diagnostics must keep the same expansion notes.

#define STRING "first"
#define TAIL value
#define WHOLE [[clang::suppress(STRING value)]]
#define NESTED [[clang::suppress("first" TAIL)]]
#define SUBSCRIPT "first"[0]
#define WRAP(X) [[clang::suppress(X)]]
#define OUTER(X) WRAP(X)
#define GNU(X) __attribute__((suppress(X)))
#define MIXED [[clang::suppress("first" TAIL)]] GNU("kept")
#define EMPTY
#define GNU_END )))
#define C23_END )]]
#define BROKEN_C23 [[clang::suppress("first"]]
#define FORWARD(X) X

#if BUSTER_CASE == 13 || BUSTER_CASE == 14
void suppress_scaffold_guard(void)
{
#if BUSTER_CASE == 14
    // The failed string consumes the first ']', leaving a complete list before the later parse error.
    [[clang::suppress("\x80"]]] if () {}
#else
    [[clang::suppress(1]] if () {}
#endif
}
#else
int suppress_macro_delimiters(int value)
{
#if BUSTER_CASE == 0
    [[clang::suppress(STRING value)]] value++;
#elif BUSTER_CASE == 1
    [[clang::suppress("first" TAIL)]] value++;
#elif BUSTER_CASE == 2
    [[clang::suppress(STRING TAIL)]] value++;
#elif BUSTER_CASE == 3
    WHOLE value++;
#elif BUSTER_CASE == 4
    NESTED value++;
#elif BUSTER_CASE == 5
    WRAP("first" value) value++;
#elif BUSTER_CASE == 6
    OUTER(SUBSCRIPT) value++;
#elif BUSTER_CASE == 7
    GNU("first" value) value++;
#elif BUSTER_CASE == 8
    MIXED value++;
#elif BUSTER_CASE == 9
    [[clang::suppress("first" EMPTY value)]] value++;
#elif BUSTER_CASE == 10
    __attribute__((suppress("first" GNU_END value++;
    [[clang::suppress("first" C23_END value++;
    OUTER(STRING) value++;
#elif BUSTER_CASE == 11
    BROKEN_C23 value++;
#elif BUSTER_CASE == 12
    FORWARD(BROKEN_C23) value++;
#endif
#if BUSTER_CASE == 10
    return value;
#else
    return undeclared_after_macro;
#endif
}
#endif

#if BUSTER_CASE == 10
int main(void)
{
    return suppress_macro_delimiters(0) != 3;
}
#endif
