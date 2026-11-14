typedef long long L8 __attribute__((vector_size(64)));
typedef int I8 __attribute__((vector_size(32)));
void f(L8 q, I8 e, void* o) { __builtin_ia32_scatterdiv16si(o, 0xff, q, e, 16); }
