// Signed narrow lanes, 64-bit multiply/divide, and floating comparison masks
// exercise scalar MIR expansion independently of the NEON arithmetic forms.
typedef signed char Bytes __attribute__((vector_size(8)));
typedef short Shorts __attribute__((vector_size(16)));
typedef long long Longs __attribute__((vector_size(16)));
typedef __INT64_TYPE__ Masks __attribute__((vector_size(16)));
typedef __INT64_TYPE__ OtherMasks __attribute__((vector_size(16)));
typedef unsigned int UnsignedWords __attribute__((vector_size(16)));
typedef double Doubles __attribute__((vector_size(16)));

static volatile int seed = 3;

int main(void)
{
    int result = 0;
    Bytes a = {-27, 26, -19, 18, -11, 10, -5, 4};
    Bytes b = {3, -3, 2, -2, 3, -3, 2, -2};
    a[0] = (signed char)(-9 * seed);
    Bytes quotient = a / b;
    Bytes remainder = a % b;
    Bytes shifted = a >> (Bytes){1, 1, 1, 1, 1, 1, 1, 1};
    Bytes less = a < b;
    Bytes negated = -a;
    Bytes inverted = ~a;
    for (int index = 0; index < 8; index += 1)
    {
        result |= quotient[index] != a[index] / b[index];
        result |= remainder[index] != a[index] % b[index];
        result |= shifted[index] != (a[index] >> 1);
        result |= less[index] != (a[index] < b[index] ? -1 : 0);
        result |= negated[index] != -a[index];
        result |= inverted[index] != (signed char)~a[index];
    }
    Shorts sa = {-30000, 30000, -27000, 27000, -19000, 19000, -11000, 11000};
    Shorts sb = {7, -7, 11, -11, 13, -13, 17, -17};
    sa[0] = (short)(-10000 * seed);
    Shorts sq = sa / sb;
    Shorts sr = sa % sb;
    Shorts sc = sa >= sb;
    for (int index = 0; index < 8; index += 1)
    {
        result |= sq[index] != sa[index] / sb[index];
        result |= sr[index] != sa[index] % sb[index];
        result |= sc[index] != (sa[index] >= sb[index] ? -1 : 0);
    }
    Longs la = {-30000000000LL, 40000000000LL};
    Longs lb = {7, -11};
    la[0] = -10000000000LL * seed;
    Longs lm = la * lb;
    Longs lq = la / lb;
    Longs lr = la % lb;
    for (int index = 0; index < 2; index += 1)
    {
        result |= lm[index] != la[index] * lb[index];
        result |= lq[index] != la[index] / lb[index];
        result |= lr[index] != la[index] % lb[index];
    }
    UnsignedWords ua = {0xfffffff0U, 0x80000000U, 0xffffffffU, 2000000000U};
    UnsignedWords ub = {7U, 65537U, 11U, 17U};
    ua[0] += (unsigned)seed;
    UnsignedWords uq = ua / ub;
    UnsignedWords ur = ua % ub;
    for (int index = 0; index < 4; index += 1)
    {
        result |= uq[index] != ua[index] / ub[index];
        result |= ur[index] != ua[index] % ub[index];
    }
    union FloatBits
    {
        Doubles vector;
        unsigned long long bits[2];
    } floating = {.bits = {0x7ff8000000000042ULL, 0x8000000000000000ULL}};
    Doubles zeros = {0.0, 0.0};
    Masks equal = floating.vector == zeros;
    Masks unequal = floating.vector != zeros;
    Masks below = floating.vector < zeros;
    Masks below_equal = floating.vector <= zeros;
    Masks above = floating.vector > zeros;
    Masks above_equal = floating.vector >= zeros;
    OtherMasks alias = equal;
    Masks alias_round_trip = alias;
    union FloatBits sign = {.vector = -floating.vector};
    result |= equal[0] != 0 || equal[1] != -1;
    result |= unequal[0] != -1 || unequal[1] != 0;
    result |= below[0] != 0 || below[1] != 0;
    result |= below_equal[0] != 0 || below_equal[1] != -1;
    result |= above[0] != 0 || above[1] != 0;
    result |= above_equal[0] != 0 || above_equal[1] != -1;
    result |= alias_round_trip[0] != 0 || alias_round_trip[1] != -1;
    result |= sign.bits[0] != 0xfff8000000000042ULL || sign.bits[1] != 0;
    return result;
}
