/* Frozen held-out family: a non-general register class. */
typedef unsigned long long U64;
typedef unsigned U32x4 __attribute__((vector_size(16)));

U64 probe(U64 x, unsigned n)
{
    U32x4 left = {(unsigned)x, (unsigned)(x >> 32), n, 19};
    U32x4 right = {3, n + 1, (unsigned)x ^ n, 23};
    U32x4 mixed = (left + right) ^ left;
    return (U64)mixed[0] + ((U64)mixed[1] << 16) + mixed[2] + mixed[3];
}
