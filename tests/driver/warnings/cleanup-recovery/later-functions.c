// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef int ByteAlignedInt __attribute__((aligned(1)));
void release_long(long* value);
void release_int(int* value);

void first(void)
{
    int a __attribute__((cleanup(release_long))) = 1;
}

void second(void)
{
    short b __attribute__((cleanup(release_long))) = 2;
    int c __attribute__((cleanup(release_long))) = 3;
}

void valid_after_errors(void)
{
    ByteAlignedInt d __attribute__((cleanup(release_int))) = 4;
    int e __attribute__((cleanup(release_int))) = 5;
}
