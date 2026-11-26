// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 source locations across grouping, literal concatenation and escapes.
void joined_format(const char *) __attribute__((format_matches(printf, 1, ("header " "%d %s"))));
void grouped_format(const char *) __attribute__((format_matches(__printf__, 1, (("%d")))));
void escaped_format(const char *) __attribute__((format_matches(printf, 1, "\x25" "d")));
void literals(void)
{
    joined_format(("header " "%s %d"));
    grouped_format((("%s")));
    escaped_format("\x25" "s");
    joined_format("header " "%d " "%s");
    grouped_format((const char *)"%d");
}
