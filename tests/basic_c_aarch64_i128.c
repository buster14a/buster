/*
 * AArch64 canonical i128 arithmetic fixture.
 *
 * The helpers write through pointers deliberately.  The current AArch64
 * caller path does not promise a direct i128 return shape yet; storing both
 * halves still exercises the canonical operation and makes stale high-half
 * results visible to a runtime oracle when this fixture is linked under
 * qemu-aarch64 (or on an AArch64 host).
 */
typedef unsigned __int128 U128;
typedef __int128 S128;
typedef unsigned int U32;
typedef unsigned long long U64;

static void unsigned_divide(U128 dividend, U128 divisor, U128 *quotient, U128 *remainder)
{
    *quotient = dividend / divisor;
    *remainder = dividend % divisor;
}

static void signed_divide(S128 dividend, S128 divisor, S128 *quotient, S128 *remainder)
{
    *quotient = dividend / divisor;
    *remainder = dividend % divisor;
}

static void count_bits(U128 value, U128 *leading, U128 *trailing)
{
    /* Buster keeps the operand's i128 width for these GNU builtins.  The
       high-half cases below therefore guard the canonical i128 lowering;
       Clang's clzll/ctzll spellings are 64-bit-only and are not used as an
       oracle for this extension. */
    *leading = __builtin_clzll(value);
    *trailing = __builtin_ctzll(value);
}

static void signed_to_double(S128 value, double *result)
{
    *result = (double)value;
}

static void unsigned_to_double(U128 value, double *result)
{
    *result = (double)value;
}

static void signed_to_float(S128 value, float *result)
{
    *result = (float)value;
}

static void unsigned_to_float(U128 value, float *result)
{
    *result = (float)value;
}

static U64 double_bits(double value)
{
    union { double f; U64 u; } value_bits = {value};
    return value_bits.u;
}

static U32 float_bits(float value)
{
    union { float f; U32 u; } value_bits = {value};
    return value_bits.u;
}

static void double_to_signed(double value, S128 *result)
{
    *result = (S128)value;
}

static void double_to_unsigned(double value, U128 *result)
{
    *result = (U128)value;
}

static void float_to_signed(float value, S128 *result)
{
    *result = (S128)value;
}

static void float_to_unsigned(float value, U128 *result)
{
    *result = (U128)value;
}

static int equal_unsigned(U128 left, U128 right)
{
    return left == right;
}

static int equal_signed(S128 left, S128 right)
{
    return left == right;
}

int main(void)
{
    U128 quotient = 0;
    U128 remainder = 0;

    /* q=0 and q=1 exercise the compare branches before/at subtraction. */
    U128 small = ((U128)0x123456789abcdef0ULL << 64) | 0x1111222233334444ULL;
    unsigned_divide(small, small + 1, &quotient, &remainder);
    if (!equal_unsigned(quotient, 0) || !equal_unsigned(remainder, small))
        return 1;
    unsigned_divide(small, small, &quotient, &remainder);
    if (!equal_unsigned(quotient, 1) || !equal_unsigned(remainder, 0))
        return 2;

    /* A high-only divisor and a carry from the low half into the high half. */
    U128 high_divisor = (U128)1 << 64;
    U128 high_dividend = high_divisor * 3 + 7;
    unsigned_divide(high_dividend, high_divisor, &quotient, &remainder);
    if (!equal_unsigned(quotient, 3) || !equal_unsigned(remainder, 7))
        return 3;
    U128 carry_dividend = ((U128)0xdeadbeefcafebabeULL << 64) | 0xffffffffffffffffULL;
    carry_dividend += 1;
    unsigned_divide(carry_dividend, 0x123456789ULL, &quotient, &remainder);
    if (!equal_unsigned(quotient * 0x123456789ULL + remainder, carry_dividend))
        return 4;

    /* Signed combinations, including INT128_MIN divided by a defined value.
       Zero divisors and INT128_MIN/-1 remain outside the fixture because the
       C frontend treats those signed-division cases as undefined behavior. */
    S128 positive = (S128)(((U128)0x123456789abcdef0ULL << 64) | 0x1111222233334444ULL);
    S128 negative = -positive;
    S128 minimum = -((S128)1 << 126) - ((S128)1 << 126);
    S128 signed_quotient = 0;
    S128 signed_remainder = 0;
    signed_divide(negative, positive, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient, -1) || !equal_signed(signed_remainder, 0))
        return 5;
    signed_divide(positive, negative, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient, -1) || !equal_signed(signed_remainder, 0))
        return 6;
    signed_divide(negative, negative, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient, 1) || !equal_signed(signed_remainder, 0))
        return 7;
    signed_divide(minimum, (S128)3, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient * 3 + signed_remainder, minimum) || signed_remainder >= 0)
        return 8;
    signed_divide((S128)23, (S128)-5, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient, -4) || !equal_signed(signed_remainder, 3))
        return 9;
    signed_divide((S128)-23, (S128)-5, &signed_quotient, &signed_remainder);
    if (!equal_signed(signed_quotient, 4) || !equal_signed(signed_remainder, -3))
        return 10;

    /* Both halves of the count result are checked, not merely a u64 cast. */
    U128 leading = 0;
    U128 trailing = 0;
    count_bits((U128)1 << 127, &leading, &trailing);
    if (!equal_unsigned(leading, 0) || !equal_unsigned(trailing, 127))
        return 11;
    count_bits((U128)1, &leading, &trailing);
    if (!equal_unsigned(leading, 127) || !equal_unsigned(trailing, 0))
        return 12;
    /* Float boundaries stay inside the C conversion-defined ranges. */
    double converted = 0.0;
    signed_to_double((S128)3, &converted);
    if (converted != 3.0)
        return 13;
    signed_to_double(-((S128)1 << 100), &converted);
    if (converted != -(double)((S128)1 << 100))
        return 14;
    unsigned_to_double((U128)1 << 127, &converted);
    if (converted != 0x1p127)
        return 15;
    /* Integer-to-double ties retain the discarded low-half sticky bit. */
    U128 double_tie = ((U128)1 << 127) + ((U128)1 << 74);
    unsigned_to_double(double_tie - 1, &converted);
    if (double_bits(converted) != 0x47e0000000000000ULL)
        return 16;
    unsigned_to_double(double_tie, &converted);
    if (double_bits(converted) != 0x47e0000000000000ULL)
        return 17;
    unsigned_to_double(double_tie + 1, &converted);
    if (double_bits(converted) != 0x47e0000000000001ULL)
        return 18;
    S128 signed_double_tie = ((S128)1 << 126) + ((S128)1 << 73);
    signed_to_double(signed_double_tie - 1, &converted);
    if (double_bits(converted) != 0x47d0000000000000ULL)
        return 19;
    signed_to_double(signed_double_tie, &converted);
    if (double_bits(converted) != 0x47d0000000000000ULL)
        return 20;
    signed_to_double(signed_double_tie + 1, &converted);
    if (double_bits(converted) != 0x47d0000000000001ULL)
        return 21;
    signed_to_double(-(signed_double_tie - 1), &converted);
    if (double_bits(converted) != 0xc7d0000000000000ULL)
        return 22;
    signed_to_double(-signed_double_tie, &converted);
    if (double_bits(converted) != 0xc7d0000000000000ULL)
        return 23;
    signed_to_double(-(signed_double_tie + 1), &converted);
    if (double_bits(converted) != 0xc7d0000000000001ULL)
        return 24;
    /* The same tie probes at binary32 precision, with a signed pair too. */
    U128 float_tie = ((U128)1 << 127) + ((U128)1 << 103);
    float float_converted = 0.0f;
    unsigned_to_float(float_tie - 1, &float_converted);
    if (float_bits(float_converted) != 0x7f000000U)
        return 25;
    unsigned_to_float(float_tie, &float_converted);
    if (float_bits(float_converted) != 0x7f000000U)
        return 26;
    unsigned_to_float(float_tie + 1, &float_converted);
    if (float_bits(float_converted) != 0x7f000001U)
        return 27;
    S128 signed_float_tie = ((S128)1 << 126) + ((S128)1 << 102);
    signed_to_float(signed_float_tie - 1, &float_converted);
    if (float_bits(float_converted) != 0x7e800000U)
        return 28;
    signed_to_float(signed_float_tie, &float_converted);
    if (float_bits(float_converted) != 0x7e800000U)
        return 29;
    signed_to_float(signed_float_tie + 1, &float_converted);
    if (float_bits(float_converted) != 0x7e800001U)
        return 30;
    S128 signed_value = 0;
    U128 unsigned_value = 0;
    double_to_signed(-0x1p127, &signed_value);
    if (!equal_signed(signed_value, minimum))
        return 31;
    double_to_signed(0x1p126, &signed_value);
    if (!equal_signed(signed_value, (S128)1 << 126))
        return 32;
    double_to_unsigned(0x1p127, &unsigned_value);
    if (!equal_unsigned(unsigned_value, (U128)1 << 127))
        return 33;
    float_to_signed(-0x1p127f, &signed_value);
    if (!equal_signed(signed_value, minimum))
        return 34;
    float_to_signed(0x1p126f, &signed_value);
    if (!equal_signed(signed_value, (S128)1 << 126))
        return 35;
    float_to_unsigned(0x1p127f, &unsigned_value);
    if (!equal_unsigned(unsigned_value, (U128)1 << 127))
        return 36;
    return 0;
}
