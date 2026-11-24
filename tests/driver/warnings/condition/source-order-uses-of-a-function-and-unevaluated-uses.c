void m(int x) __attribute__((diagnose_if(x > 0, "w1", "warning"), diagnose_if(x > 1, "w2", "warning")));
void m(int x) __attribute__((diagnose_if(x > 2, "w3", "warning")));
void r(void) __attribute__((diagnose_if(1, "u1", "warning"), diagnose_if(1, "u2", "warning")));
void a(void) __attribute__((diagnose_if(1, "always", "warning")));
void a(void) { a(); }
int h(void) { m(3); m(1); r(); void (*w)(void) = r; (void)w; (void)sizeof(a); __typeof__(a) *p = 0; (void)p; (void)_Generic(a, default: 1); return 0; }
