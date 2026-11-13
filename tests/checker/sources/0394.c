int g(int x) { return x; }
__attribute__((target_clones("avx2", "default"), target("default"))) int h(int x) { return x + 1; }
int caller(int x) { return h(x); }
