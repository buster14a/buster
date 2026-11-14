// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1842 diagnostic and recovery fixtures. Clang is an external oracle.

#if COUNTED_CASE == 0

struct ValidCounts
{
    int count;
    int *elements __attribute__((counted_by(count)));
    void *bytes __attribute__((sized_by(count)));
    int *optional_elements __attribute__((counted_by_or_null(count)));
    void *optional_bytes __attribute__((sized_by_or_null(count)));
};

int valid_member(struct ValidCounts *value)
{
    return value->count + _Alignof(int) + __alignof__(value->count);
}

#elif COUNTED_CASE == 1

int global_count;
struct CountedAlignof
{
    int count;
    int *elements __attribute__((counted_by(_Alignof(count))));
    void *bytes __attribute__((sized_by(alignof(count))));
    int *nested __attribute__((counted_by_or_null(sizeof(_Alignof(count)))));
    void *global __attribute__((sized_by_or_null(_Alignof(global_count))));
};

int warning_later = unresolved_warning;

#elif COUNTED_CASE == 2

struct CountValue
{
    int value;
};

struct MissingCountMember
{
    struct CountValue count;
    int *elements __attribute__((counted_by(count.missing)));
};

struct NotCountPointer
{
    int count;
    void *bytes __attribute__((sized_by(count->value)));
};

struct NotCountRecord
{
    int *count;
    int *elements __attribute__((counted_by_or_null(count->value)));
};

struct NotCountObject
{
    int count;
    void *bytes __attribute__((sized_by_or_null(count.value)));
};

int member_later = unresolved_member;

#elif COUNTED_CASE == 3

struct ExtraCountArguments
{
    int count;
    int *elements __attribute__((counted_by(count, 1)));
    void *bytes __attribute__((sized_by(count, (1 + 2))));
    int *optional_elements __attribute__((counted_by_or_null(count, 1)));
    void *optional_bytes __attribute__((sized_by_or_null(count, 1)));
    int *valid_elements __attribute__((counted_by(count)));
};

int extra_recovered(struct ExtraCountArguments *value)
{
    return value->count + (value->valid_elements != 0);
}

int extra_later = unresolved_extra;

#elif COUNTED_CASE == 4

struct EmptyCountArguments
{
    int count;
    int *elements __attribute__((counted_by()));
    void *bytes __attribute__((sized_by()));
    int *optional_elements __attribute__((counted_by_or_null()));
    void *optional_bytes __attribute__((sized_by_or_null()));
    int *valid_elements __attribute__((counted_by(count)));
};

int empty_recovered(struct EmptyCountArguments *value)
{
    return value->count + (value->valid_elements != 0);
}

int empty_later = unresolved_empty;

#elif COUNTED_CASE == 5

typedef int CountType;
int global_count;
struct CountedTypeOperands
{
    int count;
    int *elements __attribute__((counted_by(_Alignof(CountType))));
    void *bytes __attribute__((sized_by(alignof(int))));
    int *optional_elements __attribute__((counted_by_or_null(__alignof__(global_count))));
    void *optional_bytes __attribute__((sized_by_or_null(__alignof__(count))));
};

#elif COUNTED_CASE == 6

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-alignof-expression"
struct QuietCount
{
    int count;
    int *elements __attribute__((counted_by(_Alignof(count))));
};
#pragma clang diagnostic pop

struct AudibleCount
{
    int count;
    int *elements __attribute__((counted_by(_Alignof(count))));
};

#endif
