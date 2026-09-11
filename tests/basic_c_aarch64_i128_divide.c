/* AArch64 i128 division: exact quotients/remainders, independent shifted-
   divisor reference, and canonical control flow around split MIR blocks. */
typedef unsigned long long U64;
typedef unsigned __int128 U128;
typedef __int128 S128;

static void unsigned_divide(U128 n, U128 d, U128 *q, U128 *r)
{
    *q = n / d;
    *r = n % d;
}

static void signed_divide(S128 n, S128 d, S128 *q, S128 *r)
{
    *q = n / d;
    *r = n % d;
}

/* Align the divisor upward and subtract downward. This does not use either
   C division or the dividend-prefix loop selected for the operations above. */
static void reference(U128 n, U128 d, U128 *q, U128 *r)
{
    U64 low = (U64)n;
    U64 high = (U64)(n >> 64);
    U64 divisor_low = (U64)d;
    U64 divisor_high = (U64)(d >> 64);
    U64 bit_low = 1;
    U64 bit_high = 0;
    U64 quotient_low = 0;
    U64 quotient_high = 0;
    U64 half_low = (low >> 1) | (high << 63);
    U64 half_high = high >> 1;
    while (divisor_high < half_high || (divisor_high == half_high && divisor_low <= half_low))
    {
        divisor_high = (divisor_high << 1) | (divisor_low >> 63);
        divisor_low <<= 1;
        bit_high = (bit_high << 1) | (bit_low >> 63);
        bit_low <<= 1;
    }
    while (bit_low || bit_high)
    {
        if (high > divisor_high || (high == divisor_high && low >= divisor_low))
        {
            U64 borrow = low < divisor_low;
            low -= divisor_low;
            high -= divisor_high;
            high -= borrow;
            quotient_low |= bit_low;
            quotient_high |= bit_high;
        }
        divisor_low = (divisor_low >> 1) | (divisor_high << 63);
        divisor_high >>= 1;
        bit_low = (bit_low >> 1) | (bit_high << 63);
        bit_high >>= 1;
    }
    *q = ((U128)quotient_high << 64) | quotient_low;
    *r = ((U128)high << 64) | low;
}

static int check(U128 n, U128 d)
{
    U128 expected_q;
    U128 expected_r;
    reference(n, d, &expected_q, &expected_r);
    U128 q = ~(U128)0;
    U128 r = ~(U128)0;
    unsigned_divide(n, d, &q, &r);
    int result = q != expected_q || r != expected_r;
    U128 limit = (U128)1 << 127;
    for (unsigned signs = 0; signs < 4 && !result; signs += 1)
    {
        unsigned negative_n = signs & 1;
        unsigned negative_d = (signs >> 1) & 1;
        if (n <= limit - !negative_n && d <= limit - !negative_d &&
            !(n == limit && negative_n && d == 1 && negative_d))
        {
            U128 n_mask = (U128)0 - negative_n;
            U128 d_mask = (U128)0 - negative_d;
            U128 q_mask = (U128)0 - (negative_n != negative_d);
            U128 n_bits = (n ^ n_mask) - n_mask;
            U128 d_bits = (d ^ d_mask) - d_mask;
            U128 q_bits = (expected_q ^ q_mask) - q_mask;
            U128 r_bits = (expected_r ^ n_mask) - n_mask;
            S128 signed_q = -1;
            S128 signed_r = -1;
            signed_divide((S128)n_bits, (S128)d_bits, &signed_q, &signed_r);
            result = (U128)signed_q != q_bits || (U128)signed_r != r_bits;
        }
    }
    return result;
}

/* Several divisions in one block, scalar edge copies out of that block,
   a canonical back edge, and a switch whose targets follow split blocks. */
static U64 control_flow(U128 n, U128 d, unsigned choice)
{
    U64 result = 17;
    for (unsigned index = 0; index < 4; index += 1)
    {
        if ((choice + index) & 1)
        {
            result += (U64)(n / d);
            result ^= (U64)(n % d);
        }
        else
        {
            result ^= (U64)(n / d);
            result += (U64)(n % d);
        }
        switch ((choice + index) & 3)
        {
        case 0: result += 3; break;
        case 1: result ^= 5; break;
        case 2: result += 7; break;
        default: result ^= 11; break;
        }
    }
    return result;
}

/* A label address captured before a division must still name the entry of
   its expanded canonical block when consumed by a computed branch. */
static U64 computed_flow(U128 n, U128 d, unsigned choice)
{
    void *targets[] = {&&quotient, &&remainder};
    U64 result = (U64)(n / d);
    goto *targets[choice & 1];
quotient:
    result += 3;
remainder:
    result += 5;
    return result;
}

static U64 control_reference(U64 q, U64 r, unsigned choice)
{
    U64 result = 17;
    for (unsigned index = 0; index < 4; index += 1)
    {
        if ((choice + index) & 1)
        {
            result += q;
            result ^= r;
        }
        else
        {
            result ^= q;
            result += r;
        }
        switch ((choice + index) & 3)
        {
        case 0: result += 3; break;
        case 1: result ^= 5; break;
        case 2: result += 7; break;
        default: result ^= 11; break;
        }
    }
    return result;
}

int main(void)
{
    int result = 0;
    U128 maximum = ~(U128)0;
    for (unsigned bit = 0; bit < 128 && !result; bit += 1)
    {
        U128 power = (U128)1 << bit;
        U128 values[] = {0, 1, power - 1, power, power + 1, maximum,
                         ((U128)0xdeadbeefcafebabeULL << 64) | 0xfedcba9876543210ULL};
        U128 divisors[] = {1, 3, power, power + (bit != 0), maximum,
                           ((U128)1 << 64) - 1, ((U128)1 << 64) + 1};
        for (unsigned n = 0; n < 7 && !result; n += 1)
        {
            for (unsigned d = 0; d < 7 && !result; d += 1)
            {
                result = check(values[n], divisors[d]);
            }
        }
    }
    U64 random = 0x9e3779b97f4a7c15ULL;
    for (unsigned index = 0; index < 256 && !result; index += 1)
    {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        U128 n = ((U128)random << 64) | (random ^ 0xa5a5a5a5a5a5a5a5ULL);
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        U128 d = (((U128)random << 64) | (random >> 1) | 1) >> (index & 127);
        d |= 1;
        result = check(n, d);
        U128 q;
        U128 r;
        reference(n, d, &q, &r);
        if (!result && control_flow(n, d, index) != control_reference((U64)q, (U64)r, index)) result = 2;
        if (!result && computed_flow(n, d, index) != (U64)q + 5 + ((index & 1) ? 0 : 3)) result = 3;
    }
    return result;
}
