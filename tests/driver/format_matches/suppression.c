// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 command-line and source pragma warning state, including companion notes.
void expected_format(const char *) __attribute__((format_matches(printf, 1, "%d")));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
void muted(void) { expected_format("%s"); }
#pragma clang diagnostic pop
void active(void) { expected_format("%s"); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-insufficient-args"
void counts_muted(void) { expected_format("%d %d"); }
#pragma clang diagnostic pop
void counts_active(void) { expected_format("%d %d"); }
