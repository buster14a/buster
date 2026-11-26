// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1447 attribute argument, subject and reader-continuation cases.
extern const char *dynamic_format;
void absent_arguments(const char *) __attribute__((format_matches));
void short_arguments(const char *) __attribute__((format_matches(printf, 1)));
void excess_arguments(const char *) __attribute__((format_matches(printf, 1, "%d", 2)));
void numeric_kind(const char *) __attribute__((format_matches(17, 1, "%d")));
void string_index(const char *) __attribute__((format_matches(printf, "x", "%d")));
void zero_index(const char *) __attribute__((format_matches(printf, 0, "%d")));
void far_index(const char *) __attribute__((format_matches(printf, 2, "%d")));
void numeric_parameter(int) __attribute__((format_matches(printf, 1, "%d")));
void dynamic_expected(const char *) __attribute__((format_matches(printf, 1, dynamic_format)));
void numeric_expected(const char *) __attribute__((format_matches(printf, 1, 4)));
void cast_expected(const char *) __attribute__((format_matches(printf, 1, (const char *)"%d")));
int object_subject __attribute__((format_matches(printf, 1, "%d")));
int invalid_object_subject __attribute__((format_matches(17, 0, 4)));
void unknown_family(const char *) __attribute__((format_matches(unrecognized, 0, 4)));
int after_bad_attributes = missing_after_attributes;
