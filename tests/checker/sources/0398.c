__attribute__((always_inline, target("avx2"))) int h(int x);
__attribute__((noinline, target("default"))) int h(int x) { return x + 1; }
int caller(int x) { return h(x); }
