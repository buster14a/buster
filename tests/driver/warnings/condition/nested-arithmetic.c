void f(int a, int b) __attribute__((diagnose_if((a + b) * 2 > 10 || (a ^ b) == 7, "mix", "warning")));
void g(void){ f(3, 3); f(3, 4); f(1, 1); f(-5, 2); }
