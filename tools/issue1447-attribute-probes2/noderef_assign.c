int *p __attribute__((noderef)); int *q; int f(void) { q = p; return 0; }
