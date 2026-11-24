int a[4], b;
void f(int *p, int *q) __attribute__((diagnose_if(p == q, "same", "warning")));
void g(int *p) __attribute__((diagnose_if(!p, "null", "warning"), diagnose_if(p ? 1 : 0, "set", "warning")));
void k(const char *s) __attribute__((diagnose_if((const void *)s != 0, "cast", "warning")));
void h(void) { f(a, &b); f(0, 0); g(a); g(0); k("x"); k(0); }
