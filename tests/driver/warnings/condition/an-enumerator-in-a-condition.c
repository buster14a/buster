enum { LIMIT = 8 }; void f(int x) __attribute__((diagnose_if(x > LIMIT, "big", "warning"))); void g(void){ f(9); f(8); f(LIMIT + 1); }
