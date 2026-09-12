#ifndef BUSTER_MANY_ARGUMENTS_EXTERNAL
#define BUSTER_MANY_ARGUMENTS_EXTERNAL 0
#endif

#include "differential/many_native_arguments.h"

// Every parameter contributes with a distinct weight; the 65 narrow inputs
// also exercise normalization after the incoming register files are exhausted.
long long many_subject_integer25(MANY_INTEGER25_PARAMETERS)
{
    return a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4 + a5 * 5 + a6 * 6 + a7 * 7 + a8 * 8 + a9 * 9 +
           a10 * 10 + a11 * 11 + a12 * 12 + a13 * 13 + a14 * 14 + a15 * 15 + a16 * 16 + a17 * 17 +
           a18 * 18 + a19 * 19 + a20 * 20 + a21 * 21 + a22 * 22 + a23 * 23 + a24 * 24 + a25 * 25;
}

double many_subject_float33(MANY_FLOAT33_PARAMETERS)
{
    return a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4 + a5 * 5 + a6 * 6 + a7 * 7 + a8 * 8 + a9 * 9 +
           a10 * 10 + a11 * 11 + a12 * 12 + a13 * 13 + a14 * 14 + a15 * 15 + a16 * 16 + a17 * 17 +
           a18 * 18 + a19 * 19 + a20 * 20 + a21 * 21 + a22 * 22 + a23 * 23 + a24 * 24 + a25 * 25 +
           a26 * 26 + a27 * 27 + a28 * 28 + a29 * 29 + a30 * 30 + a31 * 31 + a32 * 32 + a33 * 33;
}

long long many_subject_scalar65(MANY_SCALAR65_PARAMETERS)
{
    return a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4 + a5 * 5 + a6 * 6 + a7 * 7 + a8 * 8 + a9 * 9 +
           a10 * 10 + a11 * 11 + a12 * 12 + a13 * 13 + a14 * 14 + a15 * 15 + a16 * 16 + a17 * 17 +
           a18 * 18 + a19 * 19 + a20 * 20 + a21 * 21 + a22 * 22 + a23 * 23 + a24 * 24 + a25 * 25 +
           a26 * 26 + a27 * 27 + a28 * 28 + a29 * 29 + a30 * 30 + a31 * 31 + a32 * 32 + a33 * 33 +
           a34 * 34 + a35 * 35 + a36 * 36 + a37 * 37 + a38 * 38 + a39 * 39 + a40 * 40 + a41 * 41 +
           a42 * 42 + a43 * 43 + a44 * 44 + a45 * 45 + a46 * 46 + a47 * 47 + a48 * 48 + a49 * 49 +
           a50 * 50 + a51 * 51 + a52 * 52 + a53 * 53 + a54 * 54 + a55 * 55 + a56 * 56 + a57 * 57 +
           a58 * 58 + a59 * 59 + a60 * 60 + a61 * 61 + a62 * 62 + a63 * 63 + a64 * 64 + a65 * 65;
}

ManyBig many_subject_aggregate33(MANY_AGGREGATE33_PARAMETERS)
{
    ManyBig result = {
        a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4 + a5 * 5 + a6 * 6 + a7 * 7 + a8 * 8 + a9 * 9 + a10 * 10 +
        a11 * 11 + a12 * 12 + (long long)a13 * 13 + (long long)a14 * 14 + (long long)a15 * 15 +
        (long long)a16 * 16 + (long long)a17 * 17 + (long long)a18 * 18 + (long long)a19 * 19 +
        (long long)a20 * 20 + (long long)a21 * 21 + (long long)a22 * 22 + (long long)a23 * 23 +
        (long long)a24 * 24,
        a25.first * 25 + a25.second * 26 + a26.first * 26 + a26.second * 27 +
        a27.first * 27 + a27.second * 28 + a28.first * 28 + a28.second * 29 +
        a29.first * 29 + a29.second * 30 + a30.first * 30 + a30.second * 31,
        a31.first * 31 + a31.second * 32 + a31.third * 33 + (long long)a32.first * 34 +
        (long long)a32.second * 35 + (long long)a32.third * 36 + (long long)a32.fourth * 37 + a33 * 38,
    };
    return result;
}

long long many_subject_variadic65(int count, ...)
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

long long many_subject_named(MANY_NAMED_PARAMETERS)
{
    __builtin_va_list arguments;
    __builtin_va_start(arguments, last);
    long long result = __builtin_va_arg(arguments, long long) + marker + last;
    __builtin_va_end(arguments);
    return result;
}

int main(void)
{
    int failed = 0;
    failed |= many_subject_named(MANY_NAMED_VALUES) != 99;
    long long (*volatile indirect)(MANY_SCALAR65_PARAMETERS) = many_subject_scalar65;
    failed |= indirect(MANY_SCALAR65_VALUES) != -93665;
    failed |= many_subject_integer25(MANY_INTEGER25_VALUES) != 5525;
    failed |= many_subject_float33(MANY_FLOAT33_VALUES) != 12529;
    failed |= many_subject_scalar65(MANY_SCALAR65_VALUES) != -93665;
    failed |= many_subject_variadic65(MANY_VARIADIC65_VALUES) != 93665;
    ManyBig aggregate = many_subject_aggregate33(MANY_AGGREGATE33_VALUES);
    failed |= aggregate.first != 4900 || aggregate.second != 93257 || aggregate.third != 77008;
#if BUSTER_MANY_ARGUMENTS_EXTERNAL
    failed |= many_host_named(MANY_NAMED_VALUES) != 99;
    failed |= many_host_integer25(MANY_INTEGER25_VALUES) != 5525;
    failed |= many_host_float33(MANY_FLOAT33_VALUES) != 12529;
    failed |= many_host_scalar65(MANY_SCALAR65_VALUES) != -93665;
    failed |= many_host_variadic65(MANY_VARIADIC65_VALUES) != 93665;
    aggregate = many_host_aggregate33(MANY_AGGREGATE33_VALUES);
    failed |= aggregate.first != 4900 || aggregate.second != 93257 || aggregate.third != 77008;
    failed |= many_host_calls_subject();
#endif
    return failed;
}
