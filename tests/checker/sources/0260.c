typedef float __attribute__((aligned(16))) F; F x; int f(void){ return __atomic_fetch_fmaximum(&x, 1, 5); }
