// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 accepted contracts, named parameter indices and ignored checking families.
void plain_format(const char *) __attribute__((format_matches(printf, 1, "%d %s")));
void indexed_format(int, const char *) __attribute__((format_matches(__printf__, 2, ("%*.*f"))));
void ignored_format(const char *) __attribute__((format_matches(scanf, 1, "%d")));
void valid(void)
{
    plain_format("%i %s");
    plain_format("%2$s %1$d");
    plain_format("%1$d %1$d %2$s");
    indexed_format(4, "%3$*1$.*2$e");
    ignored_format("%s");
    plain_format((const char *)0);
}
