struct A { int x; int y; };
_Atomic struct A a1;
_Atomic(struct A) *pa;
int f(void) { return a1.x + pa->y; }
int g(void) { return sizeof(a1.x); }
void h(void) { a1.y = 3; int q = undeclared; (void)q; }
int k(void) { return (&a1)->x; }
int u = sizeof(pa->x);
