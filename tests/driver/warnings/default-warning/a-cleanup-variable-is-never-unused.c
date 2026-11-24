void rel(int *p);
void relv(void *p);
int f(int a) { int c __attribute__((cleanup(rel))) = a; if (a) return 1; return 2; }
void g(void) { __attribute__((cleanup(rel))) int c = 0, d = 1; }
void h(int a) { int c __attribute__((cleanup(relv))); c = a; }
void k(void) { int c __attribute__((cleanup(rel))), e; }
