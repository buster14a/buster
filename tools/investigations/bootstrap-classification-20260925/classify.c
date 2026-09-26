/* Frozen Linux x86-64 classification experiment. SUBJECT_ONLY contains only
 * the operations under test; the separately trusted-compiled observer owns
 * all expected answers and creates valid x87 images with integer operations.
 * No Buster parser, float folder, or generated comparison supplies the oracle.
 */
int classify_pointer(const long double* p, int operation);
int classify_value(long double value, int operation);
int image_finite(const long double* p);
int literal_finite(void);
int literal_infinite(void);
int literal_inf_sign(void);
int guarded_user(const long double* p);
int classify_double(double value, int operation);
int classify_float(float value, int operation);
int subject_long_double_size(void);

#ifdef SUBJECT_ONLY
int classify_pointer(const long double* p, int operation)
{
    int result = -99;
    switch (operation)
    {
    case 0: result = __builtin_isfinite(*p) != 0; break;
    case 1: result = __builtin_isinf(*p) != 0; break;
    case 2: result = __builtin_isinf_sign(*p); break;
    case 3: result = __builtin_isnan(*p) != 0; break;
    case 4: result = __builtin_signbit(*p) != 0; break;
    }
    return result;
}
int classify_value(long double value, int operation)
{
    int result = -99;
    switch (operation)
    {
    case 0: result = __builtin_isfinite(value) != 0; break;
    case 1: result = __builtin_isinf(value) != 0; break;
    case 2: result = __builtin_isinf_sign(value); break;
    case 3: result = __builtin_isnan(value) != 0; break;
    case 4: result = __builtin_signbit(value) != 0; break;
    }
    return result;
}
int image_finite(const long double* p)
{
    /* Diagnostic representation-preserving control, not a compiler patch.
     * The observer guards x87 little-endian before making this call. */
    const unsigned char* bytes = (const unsigned char*)p;
    unsigned exponent = ((unsigned)(bytes[9] & 127u) << 8) | bytes[8];
    return exponent != 32767u;
}
int literal_finite(void) { return __builtin_isfinite(0x1p4000L) != 0; }
int literal_infinite(void) { return __builtin_isinf(0x1p4000L) != 0; }
int literal_inf_sign(void) { return __builtin_isinf_sign(-0x1p4000L); }
int guarded_user(const long double* p)
{
    int result = 37;
    if (!__builtin_isfinite(*p)) result = -1;
    return result;
}
int classify_double(double value, int operation)
{
    int result = -99;
    switch (operation)
    {
    case 0: result = __builtin_isfinite(value) != 0; break;
    case 1: result = __builtin_isinf(value) != 0; break;
    case 2: result = __builtin_isinf_sign(value); break;
    case 3: result = __builtin_isnan(value) != 0; break;
    case 4: result = __builtin_signbit(value) != 0; break;
    }
    return result;
}
int classify_float(float value, int operation)
{
    int result = -99;
    switch (operation)
    {
    case 0: result = __builtin_isfinite(value) != 0; break;
    case 1: result = __builtin_isinf(value) != 0; break;
    case 2: result = __builtin_isinf_sign(value); break;
    case 3: result = __builtin_isnan(value) != 0; break;
    case 4: result = __builtin_signbit(value) != 0; break;
    }
    return result;
}
int subject_long_double_size(void) { return (int)sizeof(long double); }
#else
#include <float.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* All encodings are ordinary x87 normal/subnormal/zero/inf/quiet-NaN
 * encodings, never pseudo-denormals or unsupported/unnormal encodings. */
typedef struct Image Image;
struct Image { const char* name; uint64_t significand; uint16_t exponent_sign; };
static const Image images[] = {
    {"positive_zero", UINT64_C(0), UINT16_C(0)},
    {"negative_zero", UINT64_C(0), UINT16_C(0x8000)},
    {"positive_one", UINT64_C(0x8000000000000000), UINT16_C(0x3fff)},
    {"negative_one", UINT64_C(0x8000000000000000), UINT16_C(0xbfff)},
    {"double_max", UINT64_C(0xfffffffffffff800), UINT16_C(0x43fe)},
    {"positive_2p1024", UINT64_C(0x8000000000000000), UINT16_C(0x43ff)},
    {"negative_2p1024", UINT64_C(0x8000000000000000), UINT16_C(0xc3ff)},
    {"positive_2p4000", UINT64_C(0x8000000000000000), UINT16_C(0x4f9f)},
    {"negative_2p4000", UINT64_C(0x8000000000000000), UINT16_C(0xcf9f)},
    {"positive_max", UINT64_C(0xffffffffffffffff), UINT16_C(0x7ffe)},
    {"negative_max", UINT64_C(0xffffffffffffffff), UINT16_C(0xfffe)},
    {"smallest_normal", UINT64_C(0x8000000000000000), UINT16_C(1)},
    {"smallest_subnormal", UINT64_C(1), UINT16_C(0)},
    {"positive_inf", UINT64_C(0x8000000000000000), UINT16_C(0x7fff)},
    {"negative_inf", UINT64_C(0x8000000000000000), UINT16_C(0xffff)},
    {"positive_qnan", UINT64_C(0xc000000000000123), UINT16_C(0x7fff)},
    {"negative_qnan", UINT64_C(0xc000000000000123), UINT16_C(0xffff)}
};
static unsigned checks, failures;
static void check(const char* route, const char* name, int operation, int actual, int expected)
{
    checks += 1;
    failures += actual != expected;
    printf("route=%s input=%s operation=%d actual=%d expected=%d\n", route, name, operation, actual, expected);
}
static void expected_image(Image image, int expected[5])
{
    unsigned exponent = image.exponent_sign & 32767u;
    int sign = image.exponent_sign >> 15;
    int infinite = exponent == 32767u && image.significand == UINT64_C(0x8000000000000000);
    expected[0] = exponent != 32767u;
    expected[1] = infinite;
    expected[2] = infinite ? (sign ? -1 : 1) : 0;
    expected[3] = exponent == 32767u && !infinite;
    expected[4] = sign;
}
int main(void)
{
    int result = 0;
    uint16_t endian = 1;
    if (sizeof(long double) != 16 || LDBL_MANT_DIG != 64 || LDBL_MAX_EXP != 16384 || FLT_RADIX != 2 ||
        sizeof(double) != 8 || sizeof(float) != 4 || *(unsigned char*)&endian != 1 || subject_long_double_size() != 16)
    {
        fputs("unsupported observer ABI\n", stderr);
        result = 2;
    }
    else
    {
        printf("observer=x87-integer-image-v1 size=%zu precision=%d max_exp=%d\n", sizeof(long double), LDBL_MANT_DIG, LDBL_MAX_EXP);
        for (unsigned index = 0; index < sizeof(images) / sizeof(images[0]); index += 1)
        {
            Image image = images[index];
            unsigned char bytes[16] = {0};
            long double value;
            int expected[5];
            for (unsigned byte = 0; byte < 8; byte += 1) bytes[byte] = (unsigned char)(image.significand >> (byte * 8));
            bytes[8] = (unsigned char)image.exponent_sign;
            bytes[9] = (unsigned char)(image.exponent_sign >> 8);
            memcpy(&value, bytes, sizeof(value));
            expected_image(image, expected);
            printf("image=%s significand=%016" PRIx64 " exponent_sign=%04x\n", image.name, image.significand, (unsigned)image.exponent_sign);
            for (int operation = 0; operation < 5; operation += 1)
            {
                check("pointer", image.name, operation, classify_pointer(&value, operation), expected[operation]);
                check("by_value", image.name, operation, classify_value(value, operation), expected[operation]);
            }
            check("image_control", image.name, 0, image_finite(&value), expected[0]);
            check("user_guard", image.name, 0, guarded_user(&value), expected[0] ? 37 : -1);
        }
        check("literal", "positive_2p4000", 0, literal_finite(), 1);
        check("literal", "positive_2p4000", 1, literal_infinite(), 0);
        check("literal", "negative_2p4000", 2, literal_inf_sign(), 0);
        /* Independent, exactly specified binary32/64 controls. */
        for (unsigned index = 0; index < 4; index += 1)
        {
            static const uint64_t doubles[] = {UINT64_C(0x3ff0000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000001)};
            static const uint32_t floats[] = {UINT32_C(0x3f800000), UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fc00001)};
            static const int expected[4][5] = {{1,0,0,0,0}, {0,1,1,0,0}, {0,1,-1,0,1}, {0,0,0,1,0}};
            static const char* names[] = {"one", "positive_inf", "negative_inf", "qnan"};
            double d;
            float f;
            memcpy(&d, doubles + index, sizeof(d));
            memcpy(&f, floats + index, sizeof(f));
            for (int operation = 0; operation < 5; operation += 1)
            {
                check("double_control", names[index], operation, classify_double(d, operation), expected[index][operation]);
                check("float_control", names[index], operation, classify_float(f, operation), expected[index][operation]);
            }
        }
        printf("checks=%u failures=%u\n", checks, failures);
        result = failures != 0;
    }
    return result;
}
#endif
