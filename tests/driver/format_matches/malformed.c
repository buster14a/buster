// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 partial format decomposition retains comparison diagnostics.
void integer_format(const char *) __attribute__((format_matches(printf, 1, "%d")));
void invalid_expected(const char *) __attribute__((format_matches(printf, 1, "%Y")));
void conflicting_expected(const char *) __attribute__((format_matches(printf, 1, "%1$d %1$s")));
void malformed(void)
{
    integer_format("%");
    integer_format("%Q");
    integer_format("%@");
    integer_format("%0$d");
    integer_format("%1$d %d");
    integer_format("%2$*d");
    integer_format("%1$.*d");
    integer_format("%d\0%s");
    invalid_expected("%d");
    conflicting_expected("%s");
}
