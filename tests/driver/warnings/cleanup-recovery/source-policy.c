// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void invalid_parameter(int value);
void release_long(long* value);

void first(void)
{
    int a __attribute__((cleanup(invalid_parameter))) = 1;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Walign-mismatch"
void ignored(void)
{
    short b __attribute__((cleanup(release_long))) = 2;
}
#pragma clang diagnostic warning "-Walign-mismatch"
void warned(void)
{
    char c __attribute__((cleanup(release_long))) = 3;
}
#pragma clang diagnostic error "-Walign-mismatch"
void promoted(void)
{
    int d __attribute__((cleanup(release_long))) = 4;
}
#pragma clang diagnostic pop
void restored(void)
{
    short e __attribute__((cleanup(release_long))) = 5;
}
