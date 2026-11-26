// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
unsigned int unsigned_values[2];
char plain_values[2];
int *signed_array[1] = { unsigned_values };
unsigned char *unique_char[1] = { plain_values };
void use(const int **nested)
{
    int **local[1] = { nested };
    (void)local;
}
