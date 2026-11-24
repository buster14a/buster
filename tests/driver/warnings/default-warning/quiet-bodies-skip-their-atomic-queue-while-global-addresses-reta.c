struct A { int x; };
_Atomic(struct A) a;
int f(void) { return a.x; }
int *p = &a.x;
