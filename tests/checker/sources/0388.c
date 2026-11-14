int g(int x) { return x; }
__attribute__((hot, target("default"))) int h(int x) { return x + 1; }
int caller(int x) { return h(x); }
