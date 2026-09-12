#include "many_native_arguments.h"

// The independent compiler reconstructs arrays and reduces them in loops.
long long many_host_integer25(MANY_INTEGER25_PARAMETERS)
{
    long long values[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25};
    long long result = 0;
    for (int index = 0; index < 25; index += 1)
    {
        result += values[index] * (index + 1);
    }
    return result;
}

double many_host_float33(MANY_FLOAT33_PARAMETERS)
{
    double values[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33};
    double result = 0;
    for (int index = 0; index < 33; index += 1)
    {
        result += values[index] * (index + 1);
    }
    return result;
}

long long many_host_scalar65(MANY_SCALAR65_PARAMETERS)
{
    long long values[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64, a65};
    long long result = 0;
    for (int index = 0; index < 65; index += 1)
    {
        result += values[index] * (index + 1);
    }
    return result;
}

ManyBig many_host_aggregate33(MANY_AGGREGATE33_PARAMETERS)
{
    long long scalars[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, (long long)a13, (long long)a14, (long long)a15, (long long)a16, (long long)a17, (long long)a18, (long long)a19, (long long)a20, (long long)a21, (long long)a22, (long long)a23, (long long)a24};
    ManyPair pairs[] = {a25, a26, a27, a28, a29, a30};
    long long tail[] = {a31.first, a31.second, a31.third, (long long)a32.first, (long long)a32.second, (long long)a32.third, (long long)a32.fourth, a33};
    ManyBig result = {0, 0, 0};
    for (int index = 0; index < 24; index += 1)
    {
        result.first += scalars[index] * (index + 1);
    }
    for (int index = 0; index < 6; index += 1)
    {
        result.second += pairs[index].first * (index + 25) + pairs[index].second * (index + 26);
    }
    for (int index = 0; index < 8; index += 1)
    {
        result.third += tail[index] * (index + 31);
    }
    return result;
}

long long many_host_variadic65(int count, ...)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, count);
    long long result = 0;
    for (int index = 0; index < count; index += 1)
    {
        result += __builtin_va_arg(arguments, long long) * (index + 1);
    }
    __builtin_va_end(arguments);
    return result;
}

long long many_host_named(MANY_NAMED_PARAMETERS)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, last);
    long long result = __builtin_va_arg(arguments, long long) + marker + last;
    __builtin_va_end(arguments);
    return result;
}

int many_host_calls_subject(void)
{
    int failed = 0;
    // Runtime floating inputs keep this ABI oracle independent of literal-pool relocation support.
    volatile int host_seed = 0;
    failed |= many_subject_integer25(MANY_INTEGER25_VALUES) != 5525;
    failed |= many_subject_float33(MANY_FLOAT33_HOST_VALUES) != (double)(host_seed + 12529);
    failed |= many_subject_scalar65(MANY_SCALAR65_VALUES) != -93665;
    failed |= many_subject_variadic65(MANY_VARIADIC65_VALUES) != 93665;
    ManyBig aggregate = many_subject_aggregate33(MANY_AGGREGATE33_HOST_VALUES);
    failed |= aggregate.first != 4900 || aggregate.second != 93257 || aggregate.third != 77008;
    return failed;
}
