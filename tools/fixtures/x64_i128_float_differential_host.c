#include <stdio.h>

typedef unsigned __int128 U128;
typedef __int128 S128;
typedef unsigned long long U64;

float x64_u128_to_f32(U128 value);
double x64_u128_to_f64(U128 value);
long double x64_u128_to_f80(U128 value);
float x64_s128_to_f32(S128 value);
double x64_s128_to_f64(S128 value);
long double x64_s128_to_f80(S128 value);
U128 x64_f32_to_u128(float value);
U128 x64_f64_to_u128(double value);
U128 x64_f80_to_u128(long double value);
S128 x64_f32_to_s128(float value);
S128 x64_f64_to_s128(double value);
S128 x64_f80_to_s128(long double value);

typedef union X64LongDoubleImage X64LongDoubleImage;
union X64LongDoubleImage
{
    long double value;
    struct { U64 significand; unsigned short sign_exponent; } bits;
};

int main(void)
{
    U128 two64 = (U128)1 << 64;
    U128 two127 = (U128)1 << 127;
    U128 unsigned_values[] = {0, 1, two64 - 1, two64 + 3, two127 + 5, ~(U128)0};
    S128 signed_min = -((S128)1 << 126) * 2;
    S128 signed_values[] = {0, -1, (S128)(two64 + 3), -((S128)two64 + 3), signed_min, (S128)(two127 - 1)};
    int failures = 0;

    for (unsigned index = 0; index < sizeof(unsigned_values) / sizeof(unsigned_values[0]); index += 1)
    {
        U128 value = unsigned_values[index];
        // The all-ones u128 exceeds FLT_MAX; converting it to f32 is undefined C.
        if (index != 5)
        {
            failures += x64_u128_to_f32(value) != (float)value;
        }
        failures += x64_u128_to_f64(value) != (double)value;
        failures += x64_u128_to_f80(value) != (long double)value;
    }
    for (unsigned index = 0; index < sizeof(signed_values) / sizeof(signed_values[0]); index += 1)
    {
        S128 value = signed_values[index];
        failures += x64_s128_to_f32(value) != (float)value;
        failures += x64_s128_to_f64(value) != (double)value;
        failures += x64_s128_to_f80(value) != (long double)value;
    }

    float f32_values[] = {0.0f, 0.75f, 1.5f, 0x1p64f, 0x1p127f};
    double f64_values[] = {0.0, 0.75, 1.5, 0x1p64, 0x1p127};
    for (unsigned index = 0; index < sizeof(f32_values) / sizeof(f32_values[0]); index += 1)
    {
        float value = f32_values[index];
        failures += x64_f32_to_u128(value) != (U128)value;
        failures += x64_f32_to_s128(-value) != (S128)-value;
    }
    for (unsigned index = 0; index < sizeof(f64_values) / sizeof(f64_values[0]); index += 1)
    {
        double value = f64_values[index];
        failures += x64_f64_to_u128(value) != (U128)value;
        failures += x64_f64_to_s128(-value) != (S128)-value;
    }

    X64LongDoubleImage f80_values[] = {
        {.bits = {0, 0}},
        {.bits = {0xc000000000000000ULL, 0x3fff}},
        {.bits = {0x8000000000000001ULL, 0x403f}},
        {.bits = {0xffffffffffffffffULL, 0x407d}},
        {.bits = {0xffffffffffffffffULL, 0x407e}},
    };
    for (unsigned index = 0; index < sizeof(f80_values) / sizeof(f80_values[0]); index += 1)
    {
        long double value = f80_values[index].value;
        failures += x64_f80_to_u128(value) != (U128)value;
        if (index != 4)
        {
            failures += x64_f80_to_s128(-value) != (S128)-value;
        }
    }
    printf("x64-i128-float=%d\n", failures);
    return failures ? 1 : 0;
}
