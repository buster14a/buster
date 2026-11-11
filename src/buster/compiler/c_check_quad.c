// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __float128 constants (#1250). A CheckValue of __float128 keeps the binary128 value's exact bits in `integer` (bit 127
// its sign, bits 126..112 its exponent, bits 111..0 its fraction: the 16 bytes the object holds, little-endian), and in
// `floating` a long double mirror of it, rounded to odd: the significand's top 64 bits, its last bit set where a lower
// one is (a nonzero subnormal below the x87's least is that least). The mirror keeps what the other formats' consumers
// read from a value: its sign, its class (zero, subnormal, normal, infinity, NaN, quiet or signaling, as the formats
// share the exponent field), its truth and its order; and a value rounded to odd at 64 bits rounds once more to
// float's, double's or a half format's 24, 53, 11 or 8 bits as the exact value would have rounded. A NaN's mirror is
// the others' (check_action_nan_bits): the fraction's top 63 bits at the significand's, the quiet bit at bit 62, so
// that a narrowing keeps them as APFloat's does; one whose payload lies below those bits only keeps the significand's
// last bit, so that it stays a NaN. What the mirror can't give, the bits do: the object's bytes, a conversion to long
// double or an integer, an equality, and arithmetic. Included by c_check_actions.c after CheckValue.

#define BUSTER_CHECK_QUAD_SIGN ((unsigned __int128)1 << 127)
#define BUSTER_CHECK_QUAD_QUIET ((unsigned __int128)1 << 111)
#define BUSTER_CHECK_QUAD_FRACTION (((unsigned __int128)1 << 112) - 1)
#define BUSTER_CHECK_QUAD_EXPONENT ((unsigned __int128)0x7fff << 112)

// A binary128 value's bits as the host's __float128, and back. The host's (libgcc's or compiler-rt's soft-fp) rounds as
// IEEE 754 rounds to nearest, ties to even; its NaNs aren't APFloat's, so that no NaN takes a host operation here.
static __float128 check_quad_host(unsigned __int128 bits)
{
    __float128 result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static unsigned __int128 check_quad_from_host(__float128 value)
{
    unsigned __int128 result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static bool check_quad_is_nan(unsigned __int128 bits)
{
    return (bits & ~BUSTER_CHECK_QUAD_SIGN) > BUSTER_CHECK_QUAD_EXPONENT;
}

// The long double mirror of a binary128 value's bits, rounded to odd (above), built from its bits alone: no host
// floating operation, which would quiet a signaling NaN.
static long double check_quad_mirror(unsigned __int128 bits)
{
    const uint32_t field = (uint32_t)(bits >> 112) & 0x7fffu;
    const unsigned __int128 fraction = bits & BUSTER_CHECK_QUAD_FRACTION;
    // The fraction's top 63 bits, its lower 49 bits' sticky bit the last; a normal value's or a NaN's or an infinity's
    // integer bit above them, which a subnormal's, below the least normal of both formats, hasn't.
    uint64_t significand = (uint64_t)(fraction >> 49);
    const bool lower = (fraction & ((((unsigned __int128)1) << 49) - 1)) != 0;
    const bool nan = field == 0x7fff && fraction != 0;
    // Rounded to odd, a value's lost bits set its last; a NaN keeps its top bits, as narrowing does, and only one whose
    // payload lies wholly below them takes that bit, so that its mirror stays a NaN.
    significand |= lower && (!nan || significand == 0) ? 1 : 0;
    significand |= field != 0 ? UINT64_C(0x8000000000000000) : 0;
    const uint16_t exponent = (uint16_t)(field | (uint32_t)((bits >> 112) & 0x8000u));
    uint8_t bytes[16] = { 0 };
    memcpy(bytes, &significand, sizeof(significand));
    memcpy(bytes + 8, &exponent, sizeof(exponent));
    long double result = 0;
    memcpy(&result, bytes, sizeof(result));
    return result;
}

// `value` becomes a __float128 floating constant of bits `bits`, its mirror with them.
static void check_quad_set(CheckValue* value, unsigned __int128 bits)
{
    value->integer = bits;
    value->floating = check_quad_mirror(bits);
    value->constant_kind = CHECK_CONSTANT_FLOATING;
}

// A long double's value as binary128 bits, exactly: any value of float, double, long double or a half format, as the
// checker's long double holds it. A NaN's fraction is the significand's 63 bits below the integer bit at the fraction's
// top (check_action_nan_bits' place), its quiet bit set where `quiet` (a conversion's, as APFloat's quiets it) and its
// sign kept, its bits never passed through the host's arithmetic.
static unsigned __int128 check_quad_widen(long double value, bool quiet)
{
    unsigned __int128 result = 0;
    if (isnan(value))
    {
        uint64_t significand = 0;
        uint16_t sign_exponent = 0;
        memcpy(&significand, &value, sizeof(significand));
        memcpy(&sign_exponent, (const uint8_t*)&value + sizeof(significand), sizeof(sign_exponent));
        result = BUSTER_CHECK_QUAD_EXPONENT | ((unsigned __int128)(significand & UINT64_C(0x7fffffffffffffff)) << 49) | (quiet ? BUSTER_CHECK_QUAD_QUIET : 0);
        result |= (sign_exponent & 0x8000u) != 0 ? BUSTER_CHECK_QUAD_SIGN : 0;
    }
    else
    {
        result = check_quad_from_host((__float128)value);
    }
    return result;
}

// A binary128 value's bits rounded once to long double (ties to even), or a NaN's as APFloat narrows it: its fraction's
// top 63 bits, quieted, its sign kept.
static long double check_quad_narrow(unsigned __int128 bits)
{
    long double result = 0;
    if (check_quad_is_nan(bits))
    {
        const uint64_t significand = UINT64_C(0xc000000000000000) | (uint64_t)((bits & BUSTER_CHECK_QUAD_FRACTION) >> 49);
        const uint16_t exponent = (uint16_t)(0x7fffu | ((bits & BUSTER_CHECK_QUAD_SIGN) != 0 ? 0x8000u : 0));
        uint8_t bytes[16] = { 0 };
        memcpy(bytes, &significand, sizeof(significand));
        memcpy(bytes + 8, &exponent, sizeof(exponent));
        memcpy(&result, bytes, sizeof(result));
    }
    else
    {
        result = (long double)check_quad_host(bits);
    }
    return result;
}

// An integer's bits rounded once to binary128 (ties to even): `magnitude` and its sign, as a conversion's.
static unsigned __int128 check_quad_from_integer(bool negative, unsigned __int128 magnitude)
{
    const unsigned __int128 result = check_quad_from_host((__float128)magnitude);
    return negative ? result | BUSTER_CHECK_QUAD_SIGN : result;
}

// A _Complex __float128 constant (#1250) keeps both parts' bits: the real part's in `integer`, as a real one's, and the
// imaginary part's low and high words in `floating_storage[0]` and `imaginary_storage[0]`, which every copy, stream,
// leaf and transition keeps whole. It has no long double parts: `floating` and `imaginary` hold none of its value (their
// exponent words are zero), so that every reader of a complex value takes its parts by check_complex_component, which
// gives each its bits and its mirror.
static void check_quad_complex_set(CheckValue* value, unsigned __int128 real, unsigned __int128 imaginary)
{
    value->integer = real;
    value->floating_storage[0] = (uint64_t)imaginary;
    value->floating_storage[1] = 0;
    value->imaginary_storage[0] = (uint64_t)(imaginary >> 64);
    value->imaginary_storage[1] = 0;
    value->constant_kind = CHECK_CONSTANT_COMPLEX;
}

static unsigned __int128 check_quad_complex_imaginary(const CheckValue* value)
{
    return (unsigned __int128)value->imaginary_storage[0] << 64 | value->floating_storage[0];
}

// Whether a binary128 value is finite.
static bool check_quad_finite(unsigned __int128 bits)
{
    return (bits & BUSTER_CHECK_QUAD_EXPONENT) != BUSTER_CHECK_QUAD_EXPONENT;
}

// A finite nonzero binary128 value's ilogb: its leading bit's exponent, a subnormal's below -16382.
static int32_t check_quad_logb(unsigned __int128 bits)
{
    const uint32_t field = (uint32_t)(bits >> 112) & 0x7fffu;
    const unsigned __int128 fraction = bits & BUSTER_CHECK_QUAD_FRACTION;
    const uint64_t high = (uint64_t)(fraction >> 64);
    const int32_t top = high != 0 ? 127 - __builtin_clzll(high) : 63 - __builtin_clzll((uint64_t)fraction | 1);
    return field != 0 ? (int32_t)field - 16383 : top - 16494;
}

// `value` * 2^`power`, as APFloat's scalbn: rounded once. 2^power is a binary128 value from -16494 (a subnormal) to 16383;
// past that, steps of 2^16383 first, each exact, as a scaled value stays below 2^16384 or overflows to an infinity.
static __float128 check_quad_scale(__float128 value, int32_t power)
{
    __float128 result = value;
    int32_t left = power;
    for (uint32_t step = 0; step < 2 && left > 16383; step += 1)
    {
        result *= check_quad_host((unsigned __int128)(16383 + 16383) << 112);
        left -= 16383;
    }
    const unsigned __int128 factor = left >= -16382 ? (unsigned __int128)(uint32_t)(left + 16383) << 112 : (unsigned __int128)1 << (left + 16494);
    return result * check_quad_host(factor);
}

