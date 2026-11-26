// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void invalid_parameter(int value);
void release_long(long* value);

void first_deduced(void)
{
    __auto_type a __attribute__((cleanup(release_long))) = 1;
}

void first(void)
{
    int a __attribute__((cleanup(invalid_parameter))) = 1;
}

void deduced(void)
{
    __auto_type b __attribute__((cleanup(release_long))) = 2;
    __auto_type c __attribute__((cleanup(release_long))) = (short)3;
}
