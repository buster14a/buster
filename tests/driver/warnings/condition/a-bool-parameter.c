void f(_Bool b) __attribute__((diagnose_if(b, "true", "warning")));
void g(void){ f(5); f(0); }
