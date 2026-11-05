struct S { int x; }; struct S *p __attribute__((noderef)); int f(void) { return p->x; }
