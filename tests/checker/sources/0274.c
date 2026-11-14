typedef int I16 __attribute__((vector_size(64)));
I16 f(I16 a, const void* p) { return __builtin_ia32_gathersiv16si(a, p, a, 0xffff, 3); }
