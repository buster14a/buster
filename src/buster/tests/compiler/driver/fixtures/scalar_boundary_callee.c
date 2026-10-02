// First-party separately compiled callees. Each position is checked independently.
// The caller owns the volatile counter and pointer witnesses; padding is never read.
#include "scalar_boundary.h"

int abi_boundary_gp3(int a0, long long a1, int a2)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp4(int a0, long long a1, int a2, long long a3)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp5(int a0, long long a1, int a2, long long a3, int a4)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp6(int a0, long long a1, int a2, long long a3, int a4, long long a5)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp7(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != -24680;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp8(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != -24680 || a7 != 0x708090a0bll;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp9(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, int a8)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != -24680 || a7 != 0x708090a0bll || a8 != 97531;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp3(float a0, double a1, float a2)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp4(float a0, double a1, float a2, double a3)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp5(float a0, double a1, float a2, double a3, float a4)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125 || a4 != 5.5f;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp7(float a0, double a1, float a2, double a3, float a4, double a5, float a6)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125 || a4 != 5.5f || a5 != -6.75 || a6 != 7.25f;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp8(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125 || a4 != 5.5f || a5 != -6.75 || a6 != 7.25f || a7 != -8.5;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp9(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7, float a8)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125 || a4 != 5.5f || a5 != -6.75 || a6 != 7.25f || a7 != -8.5 || a8 != 9.75f;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_positional5(long long a0, double a1, int a2, float a3, int a4)
{
    int bad = a0 != -0x102030405ll || a1 != -2.5 || a2 != -73 || a3 != 3.75f || a4 != 13579;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_gp_fp_tail(int a0, long long a1, int a2, long long a3, int a4, long long a5, float a6, double a7, int a8)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != 1.25f || a7 != -2.5 || a8 != -24680;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_fp_gp_tail(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7, int a8, long long a9, float a10)
{
    int bad = a0 != 1.25f || a1 != -2.5 || a2 != 3.75f || a3 != -4.125 || a4 != 5.5f || a5 != -6.75 || a6 != 7.25f || a7 != -8.5 || a8 != 0x12345678 || a9 != -0x102030405ll || a10 != 9.75f;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_packed_stack(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, int a8, int a9, float a10, long long a11, double a12)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != -24680 || a7 != 0x708090a0bll || a8 != 27182 || a9 != -31415 || a10 != -0.625f || a11 != 0x112233445566ll || a12 != 13.25;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_pointer4(int a0, long long a1, int a2, long long a3, volatile int* a4)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || *a4 != 85;
    *a4 = 324;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_pointer6(int a0, long long a1, int a2, long long a3, int a4, long long a5, volatile int* a6)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || *a6 != 85;
    *a6 = 326;
    abi_boundary_calls += 1;
    return bad;
}

int abi_boundary_pointer8(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, volatile int* a8)
{
    int bad = a0 != 0x12345678 || a1 != -0x102030405ll || a2 != -73 || a3 != 0x506070809ll || a4 != 13579 || a5 != -0x304050607ll || a6 != -24680 || a7 != 0x708090a0bll || *a8 != 85;
    *a8 = 328;
    abi_boundary_calls += 1;
    return bad;
}

