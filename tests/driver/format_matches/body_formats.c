// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 body modifiers and malformed formats report at the use with exact definition notes.
extern int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void modifier_body(const char *) __attribute__((format_matches(printf, 1, "%*.*f")));
void modifier_body(const char *text)
{
    printf(text, "wide", 2, 4.0);
    printf(text, 3, "precision", 4.0);
    printf(text);
    printf(text, 3);
    printf(text, 3, 2);
    printf(text, 3, 2, 9);
}
void malformed_body(const char *) __attribute__((format_matches(printf, 1, "%Y")));
void malformed_body(const char *text)
{
    printf(text, 7);
}
