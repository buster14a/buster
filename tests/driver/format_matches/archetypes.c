// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 object conversions depend on the archetype; invalid printf references are discarded.
void object_format(const char *) __attribute__((format_matches(NSString, 1, "%@")));
void cf_format(const char *) __attribute__((format_matches(CFString, 1, "%@")));
void log_format(const char *) __attribute__((format_matches(os_log, 1, "%@")));
void rejected_reference(const char *) __attribute__((format_matches(printf, 1, "%@")));
void integer_format(const char *) __attribute__((format_matches(printf, 1, "%d")));
void archetypes(void)
{
    object_format("%d");
    object_format("%s");
    object_format("%p");
    object_format("%@");
    cf_format("%p");
    log_format("%d");
    log_format("%@");
    rejected_reference("%s");
    rejected_reference("%d");
    integer_format("%@");
}
