void f(int x) __attribute__((diagnose_if((x, x > 1), "big", "warning")));
void g(int x) __attribute__((enable_if((1, x), "e")));
void h(int x) __attribute__((diagnose_if((x + 1, 2, x > 1), "big", "error")));
void k(void) { f(2); g(1); h(0); }
