#include <stdio.h>
static int dense(int x)
{
    int result = -1;
    switch (x)
    {
        case 0: result = 3; break;
        case 1: result = 5; break;
        case 2: result = 7; break;
        case 3: result = 11; break;
        case 5: result = 13; break;
        case 7: result = 17; break;
        default: break;
    }
    return result;
}
static int wide(long x)
{
    int result = 0;
    switch (x)
    {
        case -3: result += 1;
        case -2: result += 2; break;
        case 0 ... 2: result += 4; break;
        case 4: result += 8;
        case 5: result += 16; break;
        default: result += 100; break;
    }
    return result;
}
static unsigned long long quotient(unsigned __int128 n, unsigned long long d)
{
    return (unsigned long long)(n / d);
}
int main(void)
{
    unsigned sum = 0;
    for (int i = -4; i < 10; i++)
    {
        sum = sum * 31 + (unsigned)dense(i);
        sum = sum * 7 + (unsigned)wide(i - 2);
        sum = sum * 3 + (unsigned)quotient(((unsigned __int128)(unsigned)(i + 5) << 70) + 12345, 1000003);
    }
    printf("%u\n", sum);
    return 0;
}
