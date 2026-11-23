// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void additions(char* p, signed char* s, unsigned char* u, int* q)
{
    p = p + 'x'; // refuse: string-plus-int
    s = 'x' + s; // refuse: string-plus-int
    u = u + 'x'; // refuse: string-plus-int
    p = ("text") + 'x'; // refuse: string-plus-int
    p = p + ('x'); // refuse: string-plus-int
    q = q + 'x'; // take
    p = p + 1; // take
    p += 'x'; // take
}
