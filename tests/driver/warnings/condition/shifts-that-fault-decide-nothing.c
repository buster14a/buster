void f(int x) __attribute__((diagnose_if((1 << x) > 100, "shift", "warning")));
void g(void){ f(10); f(40); f(-1); f(2); }
