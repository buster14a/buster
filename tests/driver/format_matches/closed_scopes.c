// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 redeclarations use visible predecessors, including closed local-only declarations.
void scoped(const char *);
void first_global(void)
{
    void scoped(const char *) __attribute__((format_matches(printf, 1, "%d")));
    scoped("%s");
}
void outside(void) { scoped("%s"); }
void second_global(void)
{
    void scoped(const char *) __attribute__((format_matches(printf, 1, "%s")));
    scoped("%d");
}
void first_local(void)
{
    void local_only(const char *) __attribute__((format_matches(printf, 1, "%d")));
    local_only("%s");
}
void second_local(void)
{
    void local_only(const char *) __attribute__((format_matches(printf, 1, "%s")));
    local_only("%d");
}
