// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 positional order, duplicate references and width/precision ownership.
void pair_format(const char *) __attribute__((format_matches(printf, 1, "%d %s")));
void modifier_format(const char *) __attribute__((format_matches(printf, 1, "%*.*f %s")));
void sparse_format(const char *) __attribute__((format_matches(printf, 1, "%2$d")));
void positions(void)
{
    pair_format("%2$s %1$d");
    pair_format("%1$d %1$d %2$s");
    pair_format("%1$d %1$s %2$s");
    pair_format("%1$d");
    pair_format("%1$d %3$s");
    modifier_format("%3$*1$.*2$f %4$s");
    modifier_format("%3$*2$.*1$f %4$s");
    modifier_format("%d %.*f %s");
    modifier_format("%3$*1$.*2$f %4$s %5$d");
    sparse_format("%d");
    sparse_format("%1$d %2$d");
    sparse_format("%3$d");
}
