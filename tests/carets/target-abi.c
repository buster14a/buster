// -Wpsabi: AVX vectors passed and returned without AVX (#2094).
typedef float v8 __attribute__((vector_size(32)));
v8 k(v8 x) { return x; }
v8 m(v8 x) { return k(x); }
