
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned (*Kernel)(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned sum_scale(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned divide7(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned dot(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned calls(const unsigned *, const unsigned *, unsigned, unsigned);
static unsigned reference(const unsigned *a, const unsigned *b, unsigned n, unsigned salt, unsigned kind)
{
    unsigned result = (kind == 0u || kind == 2u) ? salt : 0u;
    for (unsigned i = 0; i < n; i += 1)
    {
        unsigned x = a[i];
        if (kind == 0u) result += x + x + x + 7u;
        else if (kind == 1u) result += (x + salt) / 7u;
        else if (kind == 2u) result += x * b[i];
        else { x += salt; result += ((x << 5u) + x) ^ (x >> 7u); }
    }
    return result;
}
int main(int argc, char **argv)
{
    int result = 0;
    unsigned kind = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u;
    unsigned rounds = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 32768u;
    unsigned a[1024], b[1024];
    Kernel functions[4] = {sum_scale, divide7, dot, calls};
    if (kind >= 4u) result = 2;
    for (unsigned i = 0; i < 1024u; i += 1)
    {
        a[i] = (i * 1664525u + 1013904223u) & 1023u;
        b[i] = (i * 1103515245u + 12345u) & 2047u;
    }
    if (result == 0)
    {
        Kernel function = functions[kind];
        for (unsigned salt = 0; salt < 8u; salt += 1)
        {
            unsigned actual = function(a, b, 1024u, salt);
            unsigned expected = reference(a, b, 1024u, salt, kind);
            if (actual != expected) { printf("MISMATCH kind=%u actual=%u expected=%u\n", kind, actual, expected); result = 3; }
        }
        if (result == 0)
        {
            LARGE_INTEGER begin, end, frequency;
            unsigned checksum = 0;
            if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&begin)) result = 4;
            if (result == 0)
            {
                for (unsigned r = 0; r < rounds; r += 1) checksum += function(a, b, 1024u, r);
                if (!QueryPerformanceCounter(&end)) result = 4;
                else printf("RESULT ticks=%lld frequency=%lld checksum=%u\n", end.QuadPart - begin.QuadPart, frequency.QuadPart, checksum);
            }
        }
    }
    return result;
}
