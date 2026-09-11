/* Finite, representable float-to-i128 conversions. The reference decodes
   IEEE significands using integer operations; it performs no floating cast.
   Both limbs of prefilled output storage must be replaced. */
typedef unsigned long long U64;
typedef unsigned int U32;
typedef unsigned __int128 U128;
typedef __int128 S128;

static void double_to_unsigned(double value, U128 *output)
{
    *output = (U128)value;
}

static void double_to_signed(double value, S128 *output)
{
    *output = (S128)value;
}

static void float_to_unsigned(float value, U128 *output)
{
    *output = (U128)value;
}

static void float_to_signed(float value, S128 *output)
{
    *output = (S128)value;
}

static void decoded_magnitude(U64 fraction, unsigned exponent, unsigned fraction_bits, unsigned bias, U128 *output)
{
    *output = 0;
    if (exponent >= bias)
    {
        U128 significand = ((U128)1 << fraction_bits) | fraction;
        unsigned integer_exponent = exponent - bias;
        if (integer_exponent >= fraction_bits)
        {
            *output = significand << (integer_exponent - fraction_bits);
        }
        else
        {
            *output = significand >> (fraction_bits - integer_exponent);
        }
    }
}

static int check_double(U64 bits)
{
    union { U64 bits; double value; } input = {bits};
    unsigned exponent = (unsigned)(bits >> 52) & 2047;
    U64 fraction = bits & 0x000fffffffffffffULL;
    U128 magnitude;
    decoded_magnitude(fraction, exponent, 52, 1023, &magnitude);
    U128 sign_limit = (U128)1 << 127;
    U128 unsigned_output = ~(U128)0;
    S128 signed_output = -1;
    int result = 0;
    if (!(bits >> 63) || exponent < 1023)
    {
        double_to_unsigned(input.value, &unsigned_output);
        if (unsigned_output != magnitude)
        {
            result = 1;
        }
    }
    if (magnitude < sign_limit || ((bits >> 63) && magnitude == sign_limit))
    {
        U128 sign_mask = (U128)0 - (bits >> 63);
        U128 expected = (magnitude ^ sign_mask) - sign_mask;
        double_to_signed(input.value, &signed_output);
        if ((U128)signed_output != expected)
        {
            result = 2;
        }
    }
    return result;
}

static int check_float(U32 bits)
{
    union { U32 bits; float value; } input = {bits};
    unsigned exponent = (bits >> 23) & 255;
    U32 fraction = bits & 0x007fffffU;
    U128 magnitude;
    decoded_magnitude(fraction, exponent, 23, 127, &magnitude);
    U128 sign_limit = (U128)1 << 127;
    U128 unsigned_output = ~(U128)0;
    S128 signed_output = -1;
    int result = 0;
    if (!(bits >> 31) || exponent < 127)
    {
        float_to_unsigned(input.value, &unsigned_output);
        if (unsigned_output != magnitude)
        {
            result = 3;
        }
    }
    if (magnitude < sign_limit || ((bits >> 31) && magnitude == sign_limit))
    {
        U128 sign_mask = (U128)0 - (bits >> 31);
        U128 expected = (magnitude ^ sign_mask) - sign_mask;
        float_to_signed(input.value, &signed_output);
        if ((U128)signed_output != expected)
        {
            result = 4;
        }
    }
    return result;
}

int main(void)
{
    /* All finite exponents below 2^128, including subnormals, signed zero,
       fractions on either side of one, and both sides of every limb boundary.
       Negative unsigned inputs are confined to (-1, 0], where truncation is
       representable. Positive 2^127 and larger are only converted unsigned. */
    U64 double_fractions[] = {0, 1, 0x0007ffffffffffffULL, 0x0008000000000000ULL,
                             0x000aaaaaaaaaaaaaULL, 0x000ffffffffffffeULL, 0x000fffffffffffffULL};
    U32 float_fractions[] = {0, 1, 0x003fffffU, 0x00400000U, 0x00555555U, 0x007ffffeU, 0x007fffffU};
    int result = 0;
    for (unsigned exponent = 0; exponent <= 1150 && !result; exponent += 1)
    {
        for (unsigned fraction = 0; fraction < sizeof(double_fractions) / sizeof(double_fractions[0]) && !result; fraction += 1)
        {
            U64 bits = ((U64)exponent << 52) | double_fractions[fraction];
            result = check_double(bits);
            if (!result)
            {
                result = check_double(bits | 0x8000000000000000ULL);
            }
        }
    }
    for (unsigned exponent = 0; exponent <= 254 && !result; exponent += 1)
    {
        for (unsigned fraction = 0; fraction < sizeof(float_fractions) / sizeof(float_fractions[0]) && !result; fraction += 1)
        {
            U32 bits = (exponent << 23) | float_fractions[fraction];
            result = check_float(bits);
            if (!result)
            {
                result = check_float(bits | 0x80000000U);
            }
        }
    }
    return result;
}
