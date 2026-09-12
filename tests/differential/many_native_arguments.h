#ifndef BUSTER_TEST_MANY_NATIVE_ARGUMENTS_H
#define BUSTER_TEST_MANY_NATIVE_ARGUMENTS_H

typedef struct ManyPair { long long first, second; } ManyPair;
typedef struct ManyBig { long long first, second, third; } ManyBig;
typedef struct ManyHfa { float first, second, third, fourth; } ManyHfa;

#define MANY_INTEGER25_PARAMETERS \
    long long a1, long long a2, long long a3, long long a4, long long a5, long long a6, long long a7, \
    long long a8, long long a9, long long a10, long long a11, long long a12, long long a13, \
    long long a14, long long a15, long long a16, long long a17, long long a18, long long a19, \
    long long a20, long long a21, long long a22, long long a23, long long a24, long long a25
#define MANY_INTEGER25_VALUES \
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25

#define MANY_FLOAT33_PARAMETERS \
    double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8, double a9, \
    double a10, double a11, double a12, double a13, double a14, double a15, double a16, double a17, \
    double a18, double a19, double a20, double a21, double a22, double a23, double a24, double a25, \
    double a26, double a27, double a28, double a29, double a30, double a31, double a32, double a33
#define MANY_FLOAT33_VALUES \
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, \
    28, 29, 30, 31, 32, 33

#define MANY_SCALAR65_PARAMETERS \
    signed char a1, signed char a2, signed char a3, signed char a4, signed char a5, signed char a6, \
    signed char a7, signed char a8, signed char a9, signed char a10, signed char a11, signed char a12, \
    signed char a13, signed char a14, signed char a15, signed char a16, signed char a17, \
    signed char a18, signed char a19, signed char a20, signed char a21, signed char a22, \
    signed char a23, signed char a24, signed char a25, signed char a26, signed char a27, \
    signed char a28, signed char a29, signed char a30, signed char a31, signed char a32, \
    signed char a33, signed char a34, signed char a35, signed char a36, signed char a37, \
    signed char a38, signed char a39, signed char a40, signed char a41, signed char a42, \
    signed char a43, signed char a44, signed char a45, signed char a46, signed char a47, \
    signed char a48, signed char a49, signed char a50, signed char a51, signed char a52, \
    signed char a53, signed char a54, signed char a55, signed char a56, signed char a57, \
    signed char a58, signed char a59, signed char a60, signed char a61, signed char a62, \
    signed char a63, signed char a64, signed char a65
#define MANY_SCALAR65_VALUES \
    -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, -16, -17, -18, -19, -20, -21, -22, \
    -23, -24, -25, -26, -27, -28, -29, -30, -31, -32, -33, -34, -35, -36, -37, -38, -39, -40, -41, -42, \
    -43, -44, -45, -46, -47, -48, -49, -50, -51, -52, -53, -54, -55, -56, -57, -58, -59, -60, -61, -62, \
    -63, -64, -65

#define MANY_AGGREGATE33_PARAMETERS \
    long long a1, long long a2, long long a3, long long a4, long long a5, long long a6, long long a7, \
    long long a8, long long a9, long long a10, long long a11, long long a12, double a13, double a14, \
    double a15, double a16, double a17, double a18, double a19, double a20, double a21, double a22, \
    double a23, double a24, ManyPair a25, ManyPair a26, ManyPair a27, ManyPair a28, ManyPair a29, \
    ManyPair a30, ManyBig a31, ManyHfa a32, long long a33
#define MANY_AGGREGATE33_VALUES \
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, \
    ((ManyPair){251, 252}), ((ManyPair){261, 262}), ((ManyPair){271, 272}), ((ManyPair){281, 282}), \
    ((ManyPair){291, 292}), ((ManyPair){301, 302}), ((ManyBig){311, 312, 313}), \
    ((ManyHfa){321, 322, 323, 324}), 33
#define MANY_AGGREGATE33_HOST_VALUES \
    1, 2, 3, \
    4, 5, 6, \
    7, 8, 9, \
    10, 11, 12, \
    (double)(host_seed + 13), (double)(host_seed + 14), (double)(host_seed + 15), \
    (double)(host_seed + 16), (double)(host_seed + 17), (double)(host_seed + 18), \
    (double)(host_seed + 19), (double)(host_seed + 20), (double)(host_seed + 21), \
    (double)(host_seed + 22), (double)(host_seed + 23), (double)(host_seed + 24), \
    ((ManyPair){251, 252}), ((ManyPair){261, 262}), ((ManyPair){271, 272}), \
    ((ManyPair){281, 282}), ((ManyPair){291, 292}), ((ManyPair){301, 302}), \
    ((ManyBig){311, 312, 313}), ((ManyHfa){(float)(host_seed + 321), (float)(host_seed + 322), (float)(host_seed + 323), (float)(host_seed + 324)}), 33
#define MANY_FLOAT33_HOST_VALUES \
    (double)(host_seed + 1), (double)(host_seed + 2), (double)(host_seed + 3), \
    (double)(host_seed + 4), (double)(host_seed + 5), (double)(host_seed + 6), \
    (double)(host_seed + 7), (double)(host_seed + 8), (double)(host_seed + 9), \
    (double)(host_seed + 10), (double)(host_seed + 11), (double)(host_seed + 12), \
    (double)(host_seed + 13), (double)(host_seed + 14), (double)(host_seed + 15), \
    (double)(host_seed + 16), (double)(host_seed + 17), (double)(host_seed + 18), \
    (double)(host_seed + 19), (double)(host_seed + 20), (double)(host_seed + 21), \
    (double)(host_seed + 22), (double)(host_seed + 23), (double)(host_seed + 24), \
    (double)(host_seed + 25), (double)(host_seed + 26), (double)(host_seed + 27), \
    (double)(host_seed + 28), (double)(host_seed + 29), (double)(host_seed + 30), \
    (double)(host_seed + 31), (double)(host_seed + 32), (double)(host_seed + 33)

#define MANY_VARIADIC65_PARAMETERS \
    int count, ...
#define MANY_VARIADIC65_VALUES \
    65, 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL, 9LL, 10LL, 11LL, 12LL, 13LL, 14LL, 15LL, 16LL, 17LL, \
    18LL, 19LL, 20LL, 21LL, 22LL, 23LL, 24LL, 25LL, 26LL, 27LL, 28LL, 29LL, 30LL, 31LL, 32LL, 33LL, \
    34LL, 35LL, 36LL, 37LL, 38LL, 39LL, 40LL, 41LL, 42LL, 43LL, 44LL, 45LL, 46LL, 47LL, 48LL, 49LL, \
    50LL, 51LL, 52LL, 53LL, 54LL, 55LL, 56LL, 57LL, 58LL, 59LL, 60LL, 61LL, 62LL, 63LL, 64LL, 65LL

// 522 fixed parameters put the first anonymous slot beyond a 12-bit byte
// displacement on every AArch64 desktop ABI.
#define MANY_NAMED8(prefix) long long prefix##a, long long prefix##b, long long prefix##c, long long prefix##d, \
                           long long prefix##e, long long prefix##f, long long prefix##g, long long prefix##h
#define MANY_NAMED64(prefix) MANY_NAMED8(prefix##a), MANY_NAMED8(prefix##b), MANY_NAMED8(prefix##c), MANY_NAMED8(prefix##d), \
                            MANY_NAMED8(prefix##e), MANY_NAMED8(prefix##f), MANY_NAMED8(prefix##g), MANY_NAMED8(prefix##h)
#define MANY_NAMED_PARAMETERS long long marker, MANY_NAMED64(a), MANY_NAMED64(b), MANY_NAMED64(c), MANY_NAMED64(d), \
                              MANY_NAMED64(e), MANY_NAMED64(f), MANY_NAMED64(g), MANY_NAMED64(h), MANY_NAMED8(z), long long last, ...
#define MANY_VALUES8 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL
#define MANY_VALUES64 MANY_VALUES8, MANY_VALUES8, MANY_VALUES8, MANY_VALUES8, MANY_VALUES8, MANY_VALUES8, MANY_VALUES8, MANY_VALUES8
#define MANY_NAMED_VALUES 13LL, MANY_VALUES64, MANY_VALUES64, MANY_VALUES64, MANY_VALUES64, \
                          MANY_VALUES64, MANY_VALUES64, MANY_VALUES64, MANY_VALUES64, MANY_VALUES8, 9LL, 77LL
long long many_subject_named(MANY_NAMED_PARAMETERS);
long long many_host_named(MANY_NAMED_PARAMETERS);

long long many_subject_integer25(MANY_INTEGER25_PARAMETERS);
double many_subject_float33(MANY_FLOAT33_PARAMETERS);
long long many_subject_scalar65(MANY_SCALAR65_PARAMETERS);
ManyBig many_subject_aggregate33(MANY_AGGREGATE33_PARAMETERS);
long long many_subject_variadic65(MANY_VARIADIC65_PARAMETERS);
long long many_host_integer25(MANY_INTEGER25_PARAMETERS);
double many_host_float33(MANY_FLOAT33_PARAMETERS);
long long many_host_scalar65(MANY_SCALAR65_PARAMETERS);
ManyBig many_host_aggregate33(MANY_AGGREGATE33_PARAMETERS);
long long many_host_variadic65(MANY_VARIADIC65_PARAMETERS);
int many_host_calls_subject(void);

#endif
