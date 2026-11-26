// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 definitions check format parameters using the retained contract.
extern int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void render(const char *text) __attribute__((format_matches(printf, 1, "%d %s")));
void render(const char *text)
{
    printf(text, 7, "ready");
    printf(text, "wrong", 8);
    printf(text, 7);
    printf(text, 7, "ready", 9);
}
void direct(const char *text) __attribute__((format_matches(printf, 1, "%s")))
{
    printf(text, 8);
}
void forward(const char *unknown)
{
    render(unknown);
    render((const char *)0);
    render(0);
}
extern const char *global_format;
void nonliteral(void)
{
    render(global_format);
}
