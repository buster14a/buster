void n(int x) __attribute__((diagnose_if(x > 0, "w", "warning"), diagnose_if(x > 0, "e1", "error"), diagnose_if(x > 0, "e2", "error")));
void h(void) { n(0); n(1); }
