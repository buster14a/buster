// The writable input arrays force runtime conversion. Read constant objects
// through writable pointers so the check observes the emitted object bytes.
#define CHECK_CONVERSIONS(NAME, TYPE, REAL, A, B, C, D, EA, EB, EC, ED) \
    static volatile REAL NAME##_inputs[] = {A, B, C, D}; \
    static const TYPE NAME##_constants[] = {(TYPE)(A), (TYPE)(B), (TYPE)(C), (TYPE)(D)}; \
    static const TYPE *NAME##_observed = NAME##_constants; \
    static int NAME(void) \
    { \
        const TYPE expected[] = {EA, EB, EC, ED}; \
        int failures = 0; \
        for (unsigned i = 0; i < 4; ++i) \
        { \
            TYPE runtime = (TYPE)NAME##_inputs[i]; \
            failures += NAME##_observed[i] != expected[i]; \
            failures += NAME##_observed[i] != runtime; \
        } \
        return failures; \
    }

CHECK_CONVERSIONS(s8_double, signed char, double, -2.5, 2.5, -128.75, 127.75, -2, 2, -128, 127)
CHECK_CONVERSIONS(u8_double, unsigned char, double, -0.75, 2.5, 255.75, 0.5, 0, 2, 255, 0)
CHECK_CONVERSIONS(s16_double, short, double, -2.5, 2.5, -32768.75, 32767.75, -2, 2, -32768, 32767)
CHECK_CONVERSIONS(u16_double, unsigned short, double, -0.75, 2.5, 65535.75, -0.0, 0, 2, 65535, 0)
CHECK_CONVERSIONS(s32_double, int, double, -2.5, 2.5, -2147483648.75, 2147483647.75,
                  -2, 2, (-2147483647 - 1), 2147483647)
CHECK_CONVERSIONS(u32_double, unsigned int, double, -0.75, 2.5, 4294967295.75, 0x1p-1074,
                  0, 2, 4294967295U, 0)
CHECK_CONVERSIONS(s64_double, long long, double, -2147483648.0, -2.5, -0x1p63, 0x1.fffffffffffffp62,
                  -2147483648LL, -2, (-9223372036854775807LL - 1), 0x7ffffffffffffc00LL)
CHECK_CONVERSIONS(u64_double, unsigned long long, double, -0.75, 2.5, 0x1p63, 0x1.fffffffffffffp63,
                  0, 2, 0x8000000000000000ULL, 0xfffffffffffff800ULL)
CHECK_CONVERSIONS(s8_float, signed char, float, -2.5f, 2.5f, -128.75f, 127.75f, -2, 2, -128, 127)
CHECK_CONVERSIONS(u8_float, unsigned char, float, -0.75f, 2.5f, 255.75f, 0x1p-149f, 0, 2, 255, 0)
CHECK_CONVERSIONS(s16_float, short, float, -2.5f, 2.5f, -32768.75f, 32767.75f, -2, 2, -32768, 32767)
CHECK_CONVERSIONS(u16_float, unsigned short, float, -0.75f, 2.5f, 65535.75f, 0.0f, 0, 2, 65535, 0)
CHECK_CONVERSIONS(s32_float, int, float, -2.5f, 2.5f, -0x1p31f, 0x1.fffffep30f,
                  -2, 2, (-2147483647 - 1), 2147483520)
CHECK_CONVERSIONS(u32_float, unsigned int, float, -0.75f, 2.5f, 0x1p31f, 0x1.fffffep31f,
                  0, 2, 2147483648U, 4294967040U)
CHECK_CONVERSIONS(s64_float, long long, float, -2.5f, 2.5f, -0x1p63f, 0x1.fffffep62f,
                  -2, 2, (-9223372036854775807LL - 1), 0x7fffff8000000000LL)
CHECK_CONVERSIONS(u64_float, unsigned long long, float, -0.75f, 2.5f, 0x1p63f, 0x1.fffffep63f,
                  0, 2, 0x8000000000000000ULL, 0xffffff0000000000ULL)
CHECK_CONVERSIONS(u64_float_threshold, unsigned long long, float, 0x1p31f, 0x1p32f, 0x1.fffffep62f, 0x1.000002p63f,
                  0x80000000ULL, 0x100000000ULL, 0x7fffff8000000000ULL, 0x8000010000000000ULL)

static const __int128 signed_wide[] = {
    (__int128)-2.5, (__int128)2.5, (__int128)-0x1p64, (__int128)0x1.0000000000001p64,
    (__int128)-0x1.8p100, (__int128)0x1.8p100, (__int128)-0x1p127, (__int128)0x1.fffffffffffffp126,
    (__int128)-0x1.8p100f, (__int128)0x1.8p100f,
};
static const unsigned __int128 unsigned_wide[] = {
    (unsigned __int128)-0.75, (unsigned __int128)0x1.0000000000001p64,
    (unsigned __int128)0x1p127, (unsigned __int128)0x1.fffffffffffffp127,
    (unsigned __int128)0x1p127f, (unsigned __int128)0x1.fffffep127f,
};
static const __int128 *observed_signed_wide = signed_wide;
static const unsigned __int128 *observed_unsigned_wide = unsigned_wide;

static int check_wide(void)
{
    const unsigned long long signed_limbs[][2] = {
        {0xfffffffffffffffeULL, 0xffffffffffffffffULL}, {2, 0},
        {0, 0xffffffffffffffffULL}, {0x1000, 1},
        {0, 0xffffffe800000000ULL}, {0, 0x1800000000ULL},
        {0, 0x8000000000000000ULL}, {0, 0x7ffffffffffffc00ULL},
        {0, 0xffffffe800000000ULL}, {0, 0x1800000000ULL},
    };
    const unsigned long long unsigned_limbs[][2] = {
        {0, 0}, {0x1000, 1}, {0, 0x8000000000000000ULL}, {0, 0xfffffffffffff800ULL},
        {0, 0x8000000000000000ULL}, {0, 0xffffff0000000000ULL},
    };
    int failures = 0;
    for (unsigned i = 0; i < sizeof(signed_limbs) / sizeof(signed_limbs[0]); ++i)
    {
        unsigned __int128 value = (unsigned __int128)observed_signed_wide[i];
        failures += (unsigned long long)value != signed_limbs[i][0];
        failures += (unsigned long long)(value >> 64) != signed_limbs[i][1];
    }
    for (unsigned i = 0; i < sizeof(unsigned_limbs) / sizeof(unsigned_limbs[0]); ++i)
    {
        unsigned __int128 value = observed_unsigned_wide[i];
        failures += (unsigned long long)value != unsigned_limbs[i][0];
        failures += (unsigned long long)(value >> 64) != unsigned_limbs[i][1];
    }
    return failures;
}

int main(void)
{
    return s8_double() + u8_double() + s16_double() + u16_double() + s32_double() + u32_double() + s64_double() + u64_double() +
           s8_float() + u8_float() + s16_float() + u16_float() + s32_float() + u32_float() + s64_float() + u64_float() + u64_float_threshold() + check_wide();
}
