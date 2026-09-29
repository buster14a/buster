/* Frozen held-out family: varying and zero trip counts with nested loops. */
typedef unsigned long long U64;

U64 probe(U64 x, unsigned n)
{
    U64 sum = x;
    for (unsigned i = 0; i < n; i += 1)
    {
        U64 row = x + i;
        for (unsigned j = 0; j < (i & 7); j += 1)
        {
            row = (row ^ (row >> 7)) * 33 + j;
        }
        sum += row;
        x = (x << 1) | (x >> 63);
    }
    return sum;
}
