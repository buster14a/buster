void f(int x) __attribute__((diagnose_if(x + 1 > 0, "ovf", "warning")));
void g(void) { f(2147483647); f(1); }
