int g(int x) { return x; }
__attribute__((cpu_specific(generic), target("default"))) int h(int x) { return x + 1; }
int caller(int x) { return h(x); }
