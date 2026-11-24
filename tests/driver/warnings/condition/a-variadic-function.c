void v(int x, ...) __attribute__((diagnose_if(x > 2, "var", "warning"))); void c(void) { v(3); v(3, 1, 2); v(1, 5); }
