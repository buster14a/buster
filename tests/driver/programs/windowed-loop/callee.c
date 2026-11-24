static inline __attribute__((always_inline)) int sq(int x) { return x * x; }
int a(int x) { return sq(x) + 1; }
int b(int x) { return sq(x) - 1; }
