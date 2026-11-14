struct S;
int f(void)
{
return __builtin_omp_required_simd_align(struct S[]);
}
