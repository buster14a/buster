/* Frozen held-out family: table branches, including QUALITY's existing gate. */
typedef unsigned long long U64;

U64 probe(U64 x, unsigned n)
{
    U64 result;
    switch (n & 7)
    {
        case 0: result = x + 3; break;
        case 1: result = x * 5; break;
        case 2: result = x ^ 7; break;
        case 3: result = x << 9; break;
        case 4: result = x >> 11; break;
        case 5: result = x - 13; break;
        case 6: result = ~x; break;
        default: result = x * x; break;
    }
    return result;
}
