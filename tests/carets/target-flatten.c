// A flatten function's call of a function that needs a target feature (#2094).
static inline __attribute__((target("avx2"))) int f(void) { return 1; }
__attribute__((flatten)) int h(void) { return f(); }
