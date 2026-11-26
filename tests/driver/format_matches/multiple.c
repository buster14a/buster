// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 each annotated parameter is checked, alongside ordinary variadic format arguments.
void both(const char *, const char *)
    __attribute__((format_matches(printf, 1, "%d"), format_matches(printf, 2, "%s")));
void mixed(const char *, ...)
    __attribute__((format(printf, 1, 2), format_matches(printf, 1, "%d")));
void inherited(const char *, const char *) __attribute__((format_matches(printf, 1, "%d")));
void inherited(const char *, const char *) __attribute__((format_matches(printf, 2, "%s")));
void inherited(const char *, const char *);
void replaced(const char *) __attribute__((format_matches(printf, 1, "%d")));
void replaced(const char *) __attribute__((format_matches(printf, 1, "%s")));
void multiple(void)
{
    both("%d", "%d");
    both("%s", "%d");
    mixed("%s", 7);
    mixed("%d", "wrong");
    inherited("%s", "%s");
    inherited("%d", "%d");
    replaced("%s");
    replaced("%d");
}
