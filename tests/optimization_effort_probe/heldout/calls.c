/* Frozen held-out family: recursion-free nested calls and stack arguments. */
typedef unsigned long long U64;

static U64 leaf(U64 x, U64 y)
{
    return (x + y) ^ (x * 31);
}

static U64 combine(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g)
{
    return leaf(a,b) + leaf(c,d) + leaf(e,f) + g;
}

U64 probe(U64 x, unsigned n)
{
    U64 a = leaf(x, n), b = leaf(x ^ 7, n + 3);
    return combine(a,b,x,n,x >> 4,x << 5,71) ^ a ^ b;
}
