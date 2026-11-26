// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 type classes, signedness, lengths and conversion-count cases.
void integer_format(const char *) __attribute__((format_matches(printf, 1, "%d")));
void long_format(const char *) __attribute__((format_matches(printf, 1, "%ld")));
void text_format(const char *) __attribute__((format_matches(printf, 1, "%s")));
void floating_format(const char *) __attribute__((format_matches(printf, 1, "%f")));
void conversions(void)
{
    integer_format("%i");
    integer_format("%c");
    integer_format("%u");
    integer_format("%hhd");
    integer_format("%ld");
    integer_format("%Ld");
    integer_format("%s");
    integer_format("literal");
    integer_format("");
    integer_format("%d %d");
    long_format("%lld");
    long_format("%Ld");
    long_format("%jd");
    long_format("%lu");
    text_format("%p");
    text_format("%ls");
    text_format("%m");
    floating_format("%e");
    floating_format("%lf");
    floating_format("%Lf");
}
