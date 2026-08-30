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

/* The machine selector's direct i128 subset: pair signatures over slot-backed
   halves, carry/borrow add and subtract, per-half bitwise, constant-amount
   shifts, and the whole comparison ladder.  Each function is small enough to
   select whole, so the register-allocator lanes run the machine rows here
   while the divide and multiply helpers above keep the canonical fallback. */
static U128 add_pair(U128 left, U128 right) { return left + right; }
static U128 subtract_pair(U128 left, U128 right) { return left - right; }
static U128 and_pair(U128 left, U128 right) { return left & right; }
static U128 or_pair(U128 left, U128 right) { return left | right; }
static U128 xor_pair(U128 left, U128 right) { return left ^ right; }
static U128 shift_left_1(U128 value) { return value << 1; }
static U128 shift_left_64(U128 value) { return value << 64; }
static U128 shift_left_65(U128 value) { return value << 65; }
static U128 shift_right_1(U128 value) { return value >> 1; }
static U128 shift_right_64(U128 value) { return value >> 64; }
static U128 shift_right_127(U128 value) { return value >> 127; }
static S128 arithmetic_shift_right_1(S128 value) { return value >> 1; }
static S128 arithmetic_shift_right_65(S128 value) { return value >> 65; }
static S128 arithmetic_shift_right_127(S128 value) { return value >> 127; }
static int less_unsigned(U128 left, U128 right) { return left < right; }
static int less_equal_unsigned(U128 left, U128 right) { return left <= right; }
static int greater_unsigned(U128 left, U128 right) { return left > right; }
static int greater_equal_unsigned(U128 left, U128 right) { return left >= right; }
static int less_signed(S128 left, S128 right) { return left < right; }
static int less_equal_signed(S128 left, S128 right) { return left <= right; }
static int greater_signed(S128 left, S128 right) { return left > right; }
static int greater_equal_signed(S128 left, S128 right) { return left >= right; }

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

    /* The machine subset: a carry that crosses the half boundary and its
       inverse borrow, then a doubling checked against the shift row. */
    U128 all_low = ((U128)0x0123456789abcdefULL << 64) | 0xffffffffffffffffULL;
    if (!equal_unsigned(add_pair(all_low, 1), (U128)0x0123456789abcdf0ULL << 64))
        return 37;
    if (!equal_unsigned(subtract_pair((U128)0x0123456789abcdf0ULL << 64, 1), all_low))
        return 38;
    if (!equal_unsigned(add_pair(small, small), shift_left_1(small)))
        return 39;
    /* Per-half bitwise over byte-alternating patterns. */
    U128 pattern_a = ((U128)0xf0f0f0f0f0f0f0f0ULL << 64) | 0x00ff00ff00ff00ffULL;
    U128 pattern_b = ((U128)0x0ff00ff00ff00ff0ULL << 64) | 0x0f0f0f0f0f0f0f0fULL;
    if (!equal_unsigned(and_pair(pattern_a, pattern_b), ((U128)0x00f000f000f000f0ULL << 64) | 0x000f000f000f000fULL))
        return 40;
    if (!equal_unsigned(or_pair(pattern_a, pattern_b), ((U128)0xfff0fff0fff0fff0ULL << 64) | 0x0fff0fff0fff0fffULL))
        return 41;
    if (!equal_unsigned(xor_pair(pattern_a, pattern_b), ((U128)0xff00ff00ff00ff00ULL << 64) | 0x0ff00ff00ff00ff0ULL))
        return 42;
    /* Constant shifts across the amount boundaries: cross bits below 64,
       the identity move at exactly 64, and the sign fill above it. */
    U128 wide_bits = ((U128)0x8000000000000001ULL << 64) | 0x8000000000000001ULL;
    if (!equal_unsigned(shift_left_1(wide_bits), ((U128)0x0000000000000003ULL << 64) | 0x0000000000000002ULL))
        return 43;
    if (!equal_unsigned(shift_left_64(wide_bits), (U128)0x8000000000000001ULL << 64))
        return 44;
    if (!equal_unsigned(shift_left_65(wide_bits), (U128)0x0000000000000002ULL << 64))
        return 45;
    if (!equal_unsigned(shift_right_1(wide_bits), ((U128)0x4000000000000000ULL << 64) | 0xC000000000000000ULL))
        return 46;
    if (!equal_unsigned(shift_right_64(wide_bits), (U128)0x8000000000000001ULL))
        return 47;
    if (!equal_unsigned(shift_right_127(wide_bits), (U128)1))
        return 48;
    if (!equal_signed(arithmetic_shift_right_1((S128)wide_bits), (S128)(((U128)0xC000000000000000ULL << 64) | 0xC000000000000000ULL)))
        return 49;
    if (!equal_signed(arithmetic_shift_right_65((S128)wide_bits), (S128)(((U128)0xffffffffffffffffULL << 64) | 0xC000000000000000ULL)))
        return 50;
    if (!equal_signed(arithmetic_shift_right_127((S128)wide_bits), (S128)-1))
        return 51;
    /* Every ordering, on both sides of the high-half tie. */
    U128 same_high_small = ((U128)5 << 64) | 10;
    U128 same_high_large = ((U128)5 << 64) | 20;
    U128 higher = ((U128)6 << 64) | 1;
    if (!less_unsigned(same_high_small, same_high_large) || less_unsigned(same_high_large, same_high_small) ||
        !less_unsigned(same_high_large, higher) || less_unsigned(higher, same_high_large) || less_unsigned(higher, higher))
        return 52;
    if (!less_equal_unsigned(same_high_small, same_high_small) || !less_equal_unsigned(same_high_small, same_high_large) ||
        less_equal_unsigned(higher, same_high_large))
        return 53;
    if (!greater_unsigned(higher, same_high_large) || greater_unsigned(same_high_small, same_high_large) || greater_unsigned(higher, higher))
        return 54;
    if (!greater_equal_unsigned(higher, higher) || !greater_equal_unsigned(same_high_large, same_high_small) ||
        greater_equal_unsigned(same_high_small, same_high_large))
        return 55;
    S128 negative_deep = -(S128)(((U128)1 << 64) | 5);
    S128 negative_tiny = -(S128)5;
    S128 positive_tiny = (S128)5;
    if (!less_signed(negative_deep, negative_tiny) || !less_signed(negative_tiny, positive_tiny) ||
        less_signed(positive_tiny, negative_tiny) || less_signed(positive_tiny, positive_tiny))
        return 56;
    if (!less_equal_signed(positive_tiny, positive_tiny) || !less_equal_signed(negative_deep, negative_tiny) ||
        less_equal_signed(positive_tiny, negative_tiny))
        return 57;
    if (!greater_signed(positive_tiny, negative_tiny) || !greater_signed(negative_tiny, negative_deep) ||
        greater_signed(negative_deep, negative_tiny))
        return 58;
    if (!greater_equal_signed(negative_tiny, negative_tiny) || !greater_equal_signed(positive_tiny, negative_deep) ||
        greater_equal_signed(negative_deep, positive_tiny))
        return 59;
    /* Two negatives sharing a high half defer to the unsigned low halves. */
    S128 deep_negative_a = -(S128)(((U128)1 << 64) | 5);
    S128 deep_negative_b = -(S128)(((U128)1 << 64) | 4);
    if (!less_signed(deep_negative_a, deep_negative_b) || less_signed(deep_negative_b, deep_negative_a))
        return 60;
    return 0;
}
