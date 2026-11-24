void f(char c) __attribute__((diagnose_if(c == 'x', "x", "warning")));
void g(void){ f('x'); f(120); f(1); }
