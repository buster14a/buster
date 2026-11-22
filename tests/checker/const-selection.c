// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

extern int missing(void);
extern const int later;
static const int off = 0;
const long five = 5;
static const int array[] = { 1, 0 };
static const struct { int on; } config = { 0 };
static const union { int first; int second; } choice = { .first = 0 };
static volatile const int changing = 0;

static int kept(int n) { return n + 1; }
static int in_loop(int n) { return n + 2; }
static int at_label(int n) { return n + 3; }
static int in_union(int n) { return n + 4; }
static int external(int n) { return n + 5; }
static int in_expression(int n) { return n + 6; }

static unsigned hardware(unsigned n)
{
    return __builtin_ia32_crc32qi(n, 7);
}

int tested(int n)
{
    enum { enabled = 1 };
    const _Bool on = enabled;
    const unsigned char byte = 255;
    static const unsigned long long high = 1ULL << 63;
    const int runtime = n;
    if (off || five == 0 || !on || byte + 1 != 256 || (high >> 63) != 1)
    {
        return missing();
    }
    if (array[1] || config.on || (off && n))
    {
        return missing();
    }
    switch (five)
    {
        case 5: break;
        default: return missing();
    }
    int result = off ? missing() : (off && missing());
    result += (five || hardware(n)) - 1;
    result += (five == 0) ? ({ expression_label: in_expression(n); }) : 0;
    int calls = 0;
    if (++calls && (five == 0))
    {
        result += kept(n);
    }
    result += calls - 1;
    do
    {
        if (off)
        {
            return missing();
        }
    } while (five == 0);
    if (changing || runtime)
    {
        result += kept(n);
    }
    if (later)
    {
        result += external(n);
    }
    if (choice.second)
    {
        result += in_union(n);
    }
    while (five == 0)
    {
        result += in_loop(n);
    }
    if (n == 3)
    {
        goto inside;
    }
    if (five == 0)
    {
    inside:
        result += at_label(n);
    }
    return result;
}

const int later = 0;
