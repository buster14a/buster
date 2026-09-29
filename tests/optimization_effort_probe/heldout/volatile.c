/* Frozen held-out family: ordered volatile accesses and scalar memory. */
typedef unsigned long long U64;

U64 probe(U64 x, unsigned n)
{
    volatile U64 values[4];
    values[0] = x;
    values[1] = x ^ n;
    values[2] = x + n;
    values[3] = x * 3;
    U64 result = values[n & 3];
    values[(n + 1) & 3] = result ^ values[(n + 2) & 3];
    return values[0] + values[1] + values[2] + values[3];
}
