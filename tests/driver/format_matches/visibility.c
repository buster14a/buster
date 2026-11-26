// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 call-site snapshots and changed contracts across redeclarations.
void late_format(const char *);
void before_attribute(void) { late_format("%s"); }
void late_format(const char *) __attribute__((format_matches(printf, 1, "%d")));
void after_attribute(void) { late_format("%s"); }
void late_format(const char *);
void after_redeclaration(void) { late_format("%s"); }
void late_format(const char *) __attribute__((format_matches(printf, 1, "%s")));
void after_changed_contract(void)
{
    late_format("%d");
    late_format("%s");
}
