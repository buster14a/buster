typedef float V __attribute__((vector_size(16))); V v; int f(void){ __atomic_fetch_fmaximum(&v, v, 5); return 0; }
