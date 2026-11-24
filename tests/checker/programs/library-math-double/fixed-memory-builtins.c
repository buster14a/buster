// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int memcmp(const void*, const void*, unsigned long);
void* memmove(void*, const void*, unsigned long);

#define COMPARE(n) \
    if (memcmp(a, b, n) != (at < n ? 247 : 0) || __builtin_memcmp(b, a, n) != (at < n ? -247 : 0)) \
    { \
        return 1; \
    }

static int comparisons(void)
{
    unsigned char a[33];
    unsigned char b[33];
    for (int at = 0; at <= 32; at += 1)
    {
        for (int i = 0; i < 33; i += 1)
        {
            a[i] = i == at ? 250 : 0;
            b[i] = i == at ? 3 : i > at ? 255 : 0;
        }
        COMPARE(0)
        COMPARE(1)
        COMPARE(2)
        COMPARE(3)
        COMPARE(4)
        COMPARE(7)
        COMPARE(8)
        COMPARE(9)
        COMPARE(15)
        COMPARE(16)
        COMPARE(17)
        COMPARE(31)
        COMPARE(32)
    }
    unsigned char* left = a;
    unsigned char* right = b;
    int equal = memcmp(left++, right++, 0);
    return equal || left != a + 1 || right != b + 1;
}

// Call each spelling on unaligned ranges that overlap in opposite directions. All source bytes must survive
// until loaded, including the overlap between a partial final piece and its predecessor.
#define MOVE(n) \
    if (memmove(data + 1, data, n) != data + 1 || __builtin_memmove(data + 1, data + 2, n) != data + 1) \
    { \
        return 1; \
    }

__attribute__((target("arch=znver5")))
static int moves(int seed)
{
    unsigned char data[515];
    for (int i = 0; i < 515; i += 1)
    {
        data[i] = (unsigned char)((unsigned)seed + (unsigned)i * 37u);
    }
    MOVE(17)
    MOVE(33)
    MOVE(129)
    MOVE(255)
    MOVE(256)
    MOVE(257)
    MOVE(511)
    MOVE(512)
    unsigned hash = 0;
    for (int i = 0; i < 515; i += 1)
    {
        hash = hash * 31 + data[i];
    }
    return (int)hash;
}

int tested(int n)
{
    return comparisons() ? -1 : moves(n);
}
