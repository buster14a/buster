__attribute__((target_clones("avx2", "default"))) int f(int x) { return x; }
int g(int x) { return f(x); }
