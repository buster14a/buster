typedef float F32 __attribute__((ext_vector_type(32)));
typedef double D16 __attribute__((ext_vector_type(16)));
typedef _Float16 H1 __attribute__((ext_vector_type(1)));
typedef __bf16 B64 __attribute__((ext_vector_type(64)));
float fadd(F32 a, float start) { return __builtin_reduce_assoc_fadd(a,start); }
float fmax(F32 a) { return __builtin_reduce_max(a); }
float fmin(F32 a) { return __builtin_reduce_min(a); }
float fmaximum(F32 a) { return __builtin_reduce_maximum(a); }
double dminimum(D16 a) { return __builtin_reduce_minimum(a); }
float forder(F32 a,float start) { return __builtin_reduce_in_order_fadd(a,start); }
H1 hsqrt(H1 a) { return __builtin_elementwise_sqrt(a); }
B64 badd(B64 a, B64 b) { return a+b; }
B64 bneg(B64 a) { return -a; }
