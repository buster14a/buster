/* Integer-to-float nearest-even boundaries. The integer reference constructs
   the expected IEEE image directly, without any integer-to-floating cast. */
typedef unsigned long long U64;
typedef unsigned int U32;
typedef unsigned __int128 U128;
typedef __int128 S128;

static void unsigned_to_double(U128 value, double *output)
{
    *output = (double)value;
}

static void signed_to_double(S128 value, double *output)
{
    *output = (double)value;
}

static void unsigned_to_float(U128 value, float *output)
{
    *output = (float)value;
}

static void signed_to_float(S128 value, float *output)
{
    *output = (float)value;
}

static U64 expected_bits(U128 magnitude, unsigned precision, unsigned bias)
{
    U64 result = 0;
    if (magnitude)
    {
        U64 upper = (U64)(magnitude >> 64);
        unsigned exponent = upper ? 64 : 0;
        U64 remaining = upper ? upper : (U64)magnitude;
        while (remaining > 1)
        {
            remaining >>= 1;
            exponent += 1;
        }
        U64 significand;
        if (exponent < precision)
        {
            significand = (U64)(magnitude << (precision - 1 - exponent));
        }
        else
        {
            unsigned discarded = exponent - (precision - 1);
            U128 unit = (U128)1 << discarded;
            U128 remainder = magnitude & (unit - 1);
            significand = (U64)(magnitude >> discarded);
            if (remainder > (unit >> 1) || (remainder == (unit >> 1) && (significand & 1)))
            {
                significand += 1;
            }
            if (significand == ((U64)1 << precision))
            {
                significand >>= 1;
                exponent += 1;
            }
        }
        U64 fraction_mask = ((U64)1 << (precision - 1)) - 1;
        result = ((U64)(exponent + bias) << (precision - 1)) | ((U64)significand & fraction_mask);
    }
    return result;
}

static int check_unsigned(U128 value)
{
    union { U64 bits; double value; } wide = {0x7ff8000000000001ULL};
    union { U32 bits; float value; } narrow = {0x7fc00001U};
    unsigned_to_double(value, &wide.value);
    int result = wide.bits != expected_bits(value, 53, 1023) ? 1 : 0;
    /* Larger unsigned integers exceed FLT_MAX and are outside C's defined
       conversion range. All unsigned values fit in the double range. */
    U128 float_maximum = (U128)0xffffff << 104;
    if (value <= float_maximum)
    {
        unsigned_to_float(value, &narrow.value);
        if (narrow.bits != (U32)expected_bits(value, 24, 127))
        {
            result = 2;
        }
    }
    return result;
}

static int check_signed(U128 magnitude, int negative)
{
    union { U64 bits; double value; } wide = {0x7ff8000000000001ULL};
    union { U32 bits; float value; } narrow = {0x7fc00001U};
    U128 sign_mask = (U128)0 - (unsigned)negative;
    S128 value = (S128)((magnitude ^ sign_mask) - sign_mask);
    signed_to_double(value, &wide.value);
    signed_to_float(value, &narrow.value);
    U64 sign64 = negative && magnitude ? 0x8000000000000000ULL : 0;
    U32 sign32 = negative && magnitude ? 0x80000000U : 0;
    int result = wide.bits != (expected_bits(magnitude, 53, 1023) | sign64) ? 3 : 0;
    if (narrow.bits != ((U32)expected_bits(magnitude, 24, 127) | sign32))
    {
        result = 4;
    }
    return result;
}

static int check_magnitude(U128 value)
{
    int result = check_unsigned(value);
    U128 minimum_magnitude = (U128)1 << 127;
    if (!result && value < minimum_magnitude)
    {
        result = check_signed(value, 0);
    }
    if (!result && value <= minimum_magnitude)
    {
        result = check_signed(value, 1);
    }
    return result;
}

int main(void)
{
    int result = check_magnitude(0);
    for (unsigned exponent = 0; exponent < 128 && !result; exponent += 1)
    {
        U128 power = (U128)1 << exponent;
        U128 edges[] = {power - 1, power, power + 1, power | 0x5555555555555555ULL,
                       power | ((U128)0xaaaaaaaaaaaaaaaaULL << 64), ~(U128)0};
        for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]) && !result; i += 1)
        {
            result = check_magnitude(edges[i]);
        }
        unsigned precisions[] = {24, 53};
        for (unsigned p = 0; p < 2 && !result; p += 1)
        {
            if (exponent >= precisions[p])
            {
                U128 unit = (U128)1 << (exponent - precisions[p] + 1);
                U128 half = unit >> 1;
                /* Even and odd ties, their neighbors, and a significand
                   carry. The exponent sweep crosses the 64-bit boundary. */
                U128 ties[] = {power + half - 1, power + half, power + half + 1,
                               power + unit + half - 1, power + unit + half, power + unit + half + 1,
                               power - half - 1, power - half, power - half + 1};
                for (unsigned i = 0; i < sizeof(ties) / sizeof(ties[0]) && !result; i += 1)
                {
                    result = check_magnitude(ties[i]);
                }
            }
        }
    }
    return result;
}
