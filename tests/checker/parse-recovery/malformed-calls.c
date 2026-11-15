// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original type-first malformed calls. Each case runs as a separate translation unit.

#if BUSTER_CASE == 0
void check(void)
{
    missing_callee(int);
    after_missing;
}
#elif BUSTER_CASE == 1
int known(int value);
struct Pair { int member; } pair;
void check(void)
{
    known(int) + pair.missing;
    after_known;
}
#elif BUSTER_CASE == 2
int noncallable;
void check(void)
{
    noncallable(int);
    after_noncallable;
}
#elif BUSTER_CASE == 3
int old(int) __attribute__((deprecated("retired")));
int hidden(int) __attribute__((unavailable("private")));
void check(void)
{
    old(int);
    hidden(int);
    after_attributes;
}
#elif BUSTER_CASE == 4
void check(void)
{
    retained_callee(int);
    retained_callee(1);
    retained_callee(double);
    after_repeated;
}
#elif BUSTER_CASE == 5
#define BAD_CALL(name) name(int)
int known(int value);
void check(void)
{
    BAD_CALL(known);
    BAD_CALL(known);
    after_macros;
}
#elif BUSTER_CASE == 6
int known(int value);
void check(void)
{
    known(int unread((int[2]){1, 2})[never_read[0]]);
    after_unread;
}
#elif BUSTER_CASE == 7
int known(int value);
void check(void)
{
    known(const int);
    known(struct Tag);
    known(enum Tag);
    known(_Atomic int);
    known(__typeof__(int));
    after_types;
}
#elif BUSTER_CASE == 8
int known(int value);
int source(void);
void check(void)
{
    int value = known(int);
    (void)value;
    after_initializer;
}
#elif BUSTER_CASE == 9
int known(int value);
void check(void)
{
    else;
    known(int);
    after_rounds;
}
#elif BUSTER_CASE == 10
int known(int value);
void check(void)
{
    known(int);
    after_repair;
#elif BUSTER_CASE == 11
int old(int) __attribute__((deprecated("retired")));
void check(void)
{
    old(int);
    __attribute__((unused)) old(unread_argument);
    after_mixed;
}
#elif BUSTER_CASE == 12
int known(int value);
int take(int value);
void check(void)
{
    take(known(int));
    after_nested;
}
#elif BUSTER_CASE == 13
int known(int value) { return value + 1; }
int check(void)
{
    int value = known((int)2);
    return known(sizeof(int)) + value;
}
#elif BUSTER_CASE == 14
#define ORDINARY(name) name(int)
#define CLASSIFY(name) __attribute__((unused)) name(unread_argument)
int old(int) __attribute__((deprecated("retired")));
int hidden(int) __attribute__((unavailable("private")));
void rejected(int value)
{
    int* pointer = value;
    (void)pointer;
    CLASSIFY(old);
    ORDINARY(old);
    else;
    CLASSIFY(hidden);
    ORDINARY(hidden);
    after_mixed_macros;
}
void repaired(void)
{
    ORDINARY(old);
    CLASSIFY(old);
    after_mixed_repair;
#elif BUSTER_CASE == 15
int known(int);
void check(void)
{
    known(int; after_boundary;);
}
#elif BUSTER_CASE == 16
int known(int);
void check(void)
{
    known(int, [}]);
}
#elif BUSTER_CASE == 17
void check(void)
{
    __builtin_clz(int);
}
#elif BUSTER_CASE == 18
int printf(const char*, ...);
void check(void)
{
    printf(int);
}
#elif BUSTER_CASE == 19
struct Pair { int (*member)(int); } pair;
void check(void)
{
    pair.member(int);
}
#elif BUSTER_CASE == 20
int known(int);
void check(void)
{
    known(int, unread((int[2]){1, 2}), never_read[0]);
    after_multiargument;
}
#elif BUSTER_CASE == 21
int known(int, int);
int noncallable;
void check(void)
{
    known(int, 1, 2);
    noncallable(int, 3);
    after_valid_arguments;
}
#elif BUSTER_CASE == 22
void check(void)
{
    retained_callee(int, missing_operand);
    retained_callee(1, 2);
    after_argument_lookup;
}
#elif BUSTER_CASE == 23
int old(int, int) __attribute__((deprecated("retired")));
int hidden(int, int) __attribute__((unavailable("private")));
void check(void)
{
    old(int, missing_old);
    hidden(int, missing_hidden);
    after_argument_attributes;
}
#elif BUSTER_CASE == 24
int known(int, int, int);
void check(void)
{
    known(int, double, 3);
    after_argument_rounds;
}
#elif BUSTER_CASE == 25
int known(int, int);
int take(int, int);
struct Pair { int member; } pair;
void check(void)
{
    known(int, take(int, pair.missing));
    after_nested_arguments;
}
#elif BUSTER_CASE == 26
#define BAD_ARGUMENTS(name) name(int unread((int[2]){1, 2})[skipped_name], ((int*)1) + 1.0)
int known(int, int);
void check(void)
{
    BAD_ARGUMENTS(known);
    BAD_ARGUMENTS(known);
    after_macro_arguments;
}
#elif BUSTER_CASE == 27
int known(int, int, int);
void check(void)
{
    known(int, (void)missing_operand, 1 / 0);
    after_argument_warning;
}
#elif BUSTER_CASE == 28
int known(int, int);
void check(void)
{
    known(int,);
}
#elif BUSTER_CASE == 29
int known(int, int, int);
void check(void)
{
    known(int, 1,);
}
#elif BUSTER_CASE == 30
int known(int, int, int);
void check(void)
{
    known(int, 1,, missing_operand);
}
#endif
