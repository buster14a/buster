#include <stdio.h>
typedef unsigned _BitInt(8) U8;
typedef unsigned _BitInt(16) U16;
U8 narrow8(const U8 *, unsigned, U8);
U16 narrow16(const U16 *, unsigned, U16);
U8 clang_narrow8(const U8 *, unsigned, U8);
U16 clang_narrow16(const U16 *, unsigned, U16);

int main(void)
{
    U8 a[64];
    U16 b[64];
    for (unsigned i = 0; i < 64; ++i)
    {
        a[i] = (U8)(0xffu ^ (0x81u * i));
        b[i] = (U16)(0xffffu ^ (0x8003u * i));
    }
    unsigned checks = 0;
    for (unsigned n = 0; n <= 64; ++n)
    {
        for (unsigned start = 0; start <= 64 - n; ++start)
        {
            for (unsigned seed = 0; seed < 256; ++seed)
            {
                U8 s8 = (U8)seed;
                U16 s16 = (U16)(seed * 0x101u);
                if (narrow8(a + start, n, s8) != clang_narrow8(a + start, n, s8)) return 1;
                if (narrow16(b + start, n, s16) != clang_narrow16(b + start, n, s16)) return 1;
                checks += 2;
            }
        }
    }
    printf("%u bit-precise native comparisons passed\n", checks);
    return 0;
}
