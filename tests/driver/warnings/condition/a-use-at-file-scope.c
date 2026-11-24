void a(void) __attribute__((diagnose_if(1, "always", "warning")));
void (*p)(void) = a;
