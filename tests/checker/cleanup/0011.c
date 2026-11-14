void f(int (*p)[]);
void g(int n) { int a[n] __attribute__((cleanup(f))); }
