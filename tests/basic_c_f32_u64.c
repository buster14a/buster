// Runtime operands straddle both the erroneous 2^31 threshold and the real
// 2^63 split. Every conversion is defined, including the last float below 2^64.
static volatile float inputs[] = {
    0.0f, 1.75f, 0x1.fffffep30f, 0x1p31f, 0x1.000002p31f,
    0x1.fffffep62f, 0x1p63f, 0x1.000002p63f, 0x1.fffffep63f,
};

static unsigned long long expected[] = {
    0, 1, 2147483520ULL, 2147483648ULL, 2147483904ULL,
    0x7fffff8000000000ULL, 0x8000000000000000ULL,
    0x8000010000000000ULL, 0xffffff0000000000ULL,
};

__attribute__((noinline)) unsigned long long convert_f32_u64(float value)
{
    return (unsigned long long)value;
}

static volatile double double_inputs[] = {1.75, 0x1p31, 0x1p63, 0x1.fffffffffffffp63};
static unsigned long long double_expected[] = {1, 2147483648ULL, 0x8000000000000000ULL, 0xfffffffffffff800ULL};

__attribute__((noinline)) unsigned long long convert_f64_u64(double value)
{
    return (unsigned long long)value;
}

int main(void)
{
    int failures = 0;
    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(inputs[0]); index += 1)
    {
        failures += convert_f32_u64(inputs[index]) != expected[index];
    }
    for (unsigned int index = 0; index < sizeof(double_inputs) / sizeof(double_inputs[0]); index += 1)
    {
        failures += convert_f64_u64(double_inputs[index]) != double_expected[index];
    }
    return failures;
}
