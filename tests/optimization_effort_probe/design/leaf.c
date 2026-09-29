/* Frozen design family: constant and low-pressure scalar leaves. */
typedef unsigned long long U64;

static U64 constant_leaf(void)
{
    return 0x31415926ULL;
}

static U64 mix(U64 x)
{
    return (x ^ (x >> 17)) * 0x100000001b3ULL;
}

U64 probe(U64 x, unsigned n)
{
    return mix(x) + constant_leaf() + n;
}
