void b(void) __attribute__((diagnose_if(1, "e", "error")));
void c(void) { b(); }
