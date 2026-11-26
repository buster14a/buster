// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define RELEASE(function) __attribute__((cleanup(function)))
void invalid_parameter(int value);
void release_long(long* value);

void first(void)
{
    int a RELEASE(invalid_parameter) = 1;
}

void later(void)
{
    short b RELEASE(release_long) = 2;
    char c RELEASE(release_long) = 3;
}
