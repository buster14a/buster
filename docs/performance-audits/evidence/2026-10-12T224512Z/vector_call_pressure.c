typedef unsigned int Quad __attribute__((vector_size(16)));
typedef unsigned long long Pair __attribute__((vector_size(16)));
typedef unsigned int Words __attribute__((vector_size(32)));
typedef unsigned long long Longs __attribute__((vector_size(64)));
static volatile unsigned opaque = 3;
static unsigned long long sink;
static __attribute__((noinline)) unsigned touch(unsigned x)
{
    sink += x;
    return x * 5 + opaque;
}
static __attribute__((noinline)) unsigned long long fold_quad(Quad v)
{
    return ((unsigned long long)v[0] * 31 + v[1]) * 961 + (unsigned long long)v[2] * 29791 + v[3];
}
static __attribute__((noinline)) unsigned long long fold_pair(Pair v)
{
    return v[0] * 37 + v[1];
}
static __attribute__((noinline)) unsigned long long fold_words(Words v)
{
    unsigned long long total = 0;
    for (int i = 0; i < 8; i += 1)
    {
        total = total * 31 + v[i];
    }
    return total;
}
static __attribute__((noinline)) unsigned long long fold_longs(Longs v)
{
    unsigned long long total = 0;
    for (int i = 0; i < 8; i += 1)
    {
        total = total * 37 + v[i];
    }
    return total;
}
static __attribute__((noinline)) unsigned long long narrow_loop(unsigned rounds)
{
    Quad a0 = (Quad){0, 1, 2, 3} + opaque;
    Quad a1 = a0 + 11, a2 = a0 * 3, a3 = a0 ^ 0x55, a4 = a0 + a1, a5 = a2 - a0, a6 = a3 << 2, a7 = a4 >> 1;
    Quad a8 = a0 + 101, a9 = a1 * 7, a10 = a2 ^ a3, a11 = a4 + a5, a12 = a6 - a7, a13 = a8 >> 3, a14 = a9 << 1, a15 = a10 + 9;
    Quad a16 = a11 ^ a12, a17 = a13 + a14, a18 = a15 * 5, a19 = a16 - a17;
    Pair b0 = (Pair){0, 10} + opaque;
    Pair b1 = b0 + 13, b2 = b0 * 9, b3 = b0 ^ 0x3c3c, b4 = b1 + b2, b5 = b3 - b0, b6 = b4 << 3, b7 = b5 >> 2;
    Pair b8 = b6 + b7, b9 = b8 * 3, b10 = b9 ^ b1, b11 = b10 + b2, b12 = b11 - b3, b13 = b12 >> 1, b14 = b13 << 2, b15 = b14 + 77;
    Pair b16 = b15 ^ b0, b17 = b16 + b4;
    for (unsigned i = 0; i < rounds; i += 1)
    {
        unsigned k = touch(i);
        a0 += k; a1 ^= a0; a2 += a1; a3 ^= a2; a4 += a3; a5 ^= a4; a6 += a5; a7 ^= a6; a8 += a7; a9 ^= a8;
        a10 += a9; a11 ^= a10; a12 += a11; a13 ^= a12; a14 += a13; a15 ^= a14; a16 += a15; a17 ^= a16; a18 += a17; a19 ^= a18;
        unsigned j = touch(k);
        b0 += j; b1 ^= b0; b2 += b1; b3 ^= b2; b4 += b3; b5 ^= b4; b6 += b5; b7 ^= b6; b8 += b7;
        b9 ^= b8; b10 += b9; b11 ^= b10; b12 += b11; b13 ^= b12; b14 += b13; b15 ^= b14; b16 += b15; b17 ^= b16;
        a0 = (a0 << 1) | (a0 >> 31);
        b0 = (b0 << 3) | (b0 >> 61);
    }
    Quad qa = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8 ^ a9;
    Quad qb = a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17 + a18 + a19;
    Pair pa = b0 ^ b1 ^ b2 ^ b3 ^ b4 ^ b5 ^ b6 ^ b7 ^ b8;
    Pair pb = b9 + b10 + b11 + b12 + b13 + b14 + b15 + b16 + b17;
    return fold_quad(qa) ^ (fold_quad(qb) * 3) ^ fold_pair(pa) ^ (fold_pair(pb) * 5);
}
static __attribute__((noinline)) unsigned long long wide_calls(unsigned seed)
{
    Words w0 = (Words){0, 1, 2, 3, 4, 5, 6, 7} + seed;
    Words w1 = w0 * 3, w2 = w1 ^ w0, w3 = w2 + 17, w4 = w3 << 2, w5 = w4 - w1, w6 = w5 >> 1, w7 = w6 ^ w2;
    Longs l0 = (Longs){0, 10, 20, 30, 40, 50, 60, 70} + seed;
    Longs l1 = l0 * 9, l2 = l1 ^ l0, l3 = l2 + 33, l4 = l3 << 3, l5 = l4 - l1;
    unsigned k = touch(seed);
    w0 += k; l0 ^= k;
    unsigned j = touch(k);
    w1 ^= j; l1 += j;
    Words w = (w0 + w1) ^ (w2 + w3) ^ (w4 - w5) ^ (w6 + w7);
    Longs l = (l0 ^ l1) + (l2 ^ l3) + (l4 ^ l5);
    return fold_words(w) * 7 ^ fold_longs(l);
}
int main(void)
{
    unsigned long long narrow = narrow_loop(opaque + 5);
    unsigned long long wide = wide_calls(opaque + 40);
    return (narrow != 18446742968909630808ULL) | (wide != 439599296363116ULL) | (sink != 453);
}
