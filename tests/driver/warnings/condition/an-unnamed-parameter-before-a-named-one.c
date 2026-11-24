void f(int, int y) __attribute__((diagnose_if(y > 1, "second", "warning")));
void g(void) { f(5, 2); f(5, 0); }
