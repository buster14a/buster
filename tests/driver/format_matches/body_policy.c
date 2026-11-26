// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 body warning policy belongs to the use; its definition note stays in the same group.
extern int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void active_definition(const char *) __attribute__((format_matches(printf, 1, "%d")));
void active_definition(const char *text)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
    printf(text, "muted");
#pragma clang diagnostic pop
    printf(text, "active");
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
void muted_definition(const char *) __attribute__((format_matches(printf, 1, "%s")));
#pragma clang diagnostic pop
void muted_definition(const char *text)
{
    printf(text, 7);
}
