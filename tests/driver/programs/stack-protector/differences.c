void use(void*);
int d_aligned(void) { struct { char c; } __attribute__((aligned(16))) s; use(&s); return 0; }
int d_union_padding(void) { union { double d; char c[4]; int x[3]; } u; use(&u); return 0; }
int d_alloca_small(void) { char* p = __builtin_alloca(7); use(p); return 0; }
