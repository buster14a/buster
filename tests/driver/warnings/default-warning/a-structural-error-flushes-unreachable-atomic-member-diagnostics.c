struct A { int x; };
_Atomic(struct A) a;
void f(void) { if (0) a.x = 0; int q = undeclared; (void)q; }
