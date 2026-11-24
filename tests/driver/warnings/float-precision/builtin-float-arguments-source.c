typedef double D;
float copy(D x, long double y) { return __builtin_copysignf(x, y); }
float absolute(double x) { return __builtin_fabsf(x); }
float fused(double x, long double y) { return __builtin_fmaf(x, 1.0f, y); }
float nested(long double x) { return __builtin_copysignf(__builtin_ceil(x), __builtin_floor(x)); }
float choose(int c, double x, double y) { return __builtin_sqrtf(c ? x : y); }
float dead(double x) { if (0) return __builtin_sqrtf(x); return __builtin_ceilf(0.1); }
float exchange(_Atomic float *p, double x) { return __c11_atomic_exchange(p, x, 5); }
float exact(double x) { return __builtin_copysignf((float)x, 1.0); }
int query(long double x) { return __builtin_isfinite(x) + (int)sizeof(__builtin_floorf(x)); }
float component(_Complex double x) { return __builtin_crealf(x); }
