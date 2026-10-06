// First-party fixed-prototype scalar call contract; no target layout is inferred from the host.
#ifndef BUSTER_SCALAR_BOUNDARY_H
#define BUSTER_SCALAR_BOUNDARY_H

_Static_assert(sizeof(int) == 4 && _Alignof(int) == 4, "int layout");
_Static_assert(sizeof(long long) == 8 && _Alignof(long long) == 8, "long long layout");
_Static_assert(sizeof(float) == 4 && _Alignof(float) == 4, "float layout");
_Static_assert(sizeof(double) == 8 && _Alignof(double) == 8, "double layout");
_Static_assert(sizeof(void*) == 8 && _Alignof(void*) == 8, "pointer layout");

extern volatile int abi_boundary_calls;

int abi_boundary_gp3(int a0, long long a1, int a2);
int abi_boundary_gp4(int a0, long long a1, int a2, long long a3);
int abi_boundary_gp5(int a0, long long a1, int a2, long long a3, int a4);
int abi_boundary_gp6(int a0, long long a1, int a2, long long a3, int a4, long long a5);
int abi_boundary_gp7(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6);
int abi_boundary_gp8(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7);
int abi_boundary_gp9(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, int a8);
int abi_boundary_fp3(float a0, double a1, float a2);
int abi_boundary_fp4(float a0, double a1, float a2, double a3);
int abi_boundary_fp5(float a0, double a1, float a2, double a3, float a4);
int abi_boundary_fp7(float a0, double a1, float a2, double a3, float a4, double a5, float a6);
int abi_boundary_fp8(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7);
int abi_boundary_fp9(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7, float a8);
int abi_boundary_positional5(long long a0, double a1, int a2, float a3, int a4);
int abi_boundary_gp_fp_tail(int a0, long long a1, int a2, long long a3, int a4, long long a5, float a6, double a7, int a8);
int abi_boundary_fp_gp_tail(float a0, double a1, float a2, double a3, float a4, double a5, float a6, double a7, int a8, long long a9, float a10);
int abi_boundary_packed_stack(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, int a8, int a9, float a10, long long a11, double a12);
int abi_boundary_pointer4(int a0, long long a1, int a2, long long a3, volatile int* a4);
int abi_boundary_pointer6(int a0, long long a1, int a2, long long a3, int a4, long long a5, volatile int* a6);
int abi_boundary_pointer8(int a0, long long a1, int a2, long long a3, int a4, long long a5, int a6, long long a7, volatile int* a8);

#endif
