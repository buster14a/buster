// A floating literal's decimal-to-binary conversion must not depend on where
// the literal appears.  Every value below initializes a global — through the
// scalar, array, and aggregate initializer paths — and is then decoded again
// as a local from the same spelling; both must equal the bits Clang emits.
// The exponents deliberately sit outside the window of exactly representable
// powers of ten, where a digit-by-digit f64 accumulator drifts by ulps, and
// 1e-310 is subnormal, which such an accumulator flushes to zero.
#include <stdint.h>

union DoubleBits
{
    double value;
    uint64_t bits;
};

union FloatBits
{
    float value;
    uint32_t bits;
};

struct Pair
{
    double wide;
    float narrow;
};

static const double global_array[] = {
    1e300, -1e-300, 1e-310, 1e30, 123e+127, 4.9406564584124654e-324, 2.2250738585072011e-308,
};

static const uint64_t expected_array[] = {
    UINT64_C(0x7e37e43c8800759c), UINT64_C(0x81a56e1fc2f8f359), UINT64_C(0x000012688b70e62b), UINT64_C(0x46293e5939a08cea),
    UINT64_C(0x5abc64336586c67c), UINT64_C(0x0000000000000001), UINT64_C(0x000fffffffffffff),
};

static const double global_scalar = 1e300;

// An f suffix gives the literal type float, so a double initialized from one
// holds that float widened, not the double the decimal denotes.
static const double global_suffix = 1.1f;

static const struct Pair global_pair = {2.2250738585072011e-308, 0.1f};

// Spellings outside the finite range still saturate the way the host does.
static const double global_overflow = 1e400;
static const double global_underflow = 1e-400;

static uint64_t double_bits(double value)
{
    union DoubleBits converted;
    converted.value = value;
    return converted.bits;
}

static uint32_t float_bits(float value)
{
    union FloatBits converted;
    converted.value = value;
    return converted.bits;
}

int main(void)
{
    int failure = 0;
    unsigned count = (unsigned)(sizeof(global_array) / sizeof(global_array[0]));
    for (unsigned index = 0; index < count; index += 1)
    {
        if (double_bits(global_array[index]) != expected_array[index])
        {
            failure = 1;
        }
    }
    double local_array[] = {
        1e300, -1e-300, 1e-310, 1e30, 123e+127, 4.9406564584124654e-324, 2.2250738585072011e-308,
    };
    for (unsigned index = 0; index < count; index += 1)
    {
        if (double_bits(local_array[index]) != expected_array[index])
        {
            failure = failure ? failure : 2;
        }
    }
    if (double_bits(global_scalar) != expected_array[0])
    {
        failure = failure ? failure : 3;
    }
    if (double_bits(global_suffix) != UINT64_C(0x3ff19999a0000000))
    {
        failure = failure ? failure : 4;
    }
    if (double_bits(global_pair.wide) != expected_array[6] || float_bits(global_pair.narrow) != UINT32_C(0x3dcccccd))
    {
        failure = failure ? failure : 5;
    }
    if (double_bits(global_overflow) != UINT64_C(0x7ff0000000000000) || double_bits(global_underflow) != 0)
    {
        failure = failure ? failure : 6;
    }

    return failure;
}
