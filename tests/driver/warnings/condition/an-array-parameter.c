void f(int a[4], int n) __attribute__((diagnose_if(n > 4, "over", "warning")));
void g(int *p) { f(p, 5); }
