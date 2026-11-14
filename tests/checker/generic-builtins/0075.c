struct S;struct T{int x;};
int f(void)
{
return __builtin_omp_required_simd_align(int[]) + __builtin_omp_required_simd_align(int[3]) +
__builtin_omp_required_simd_align(struct S*) + __builtin_omp_required_simd_align(double[2][3]) + __builtin_omp_required_simd_align(struct T) +
__builtin_omp_required_simd_align(int(*)(void));
}
