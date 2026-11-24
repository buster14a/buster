struct A { int x; };
_Atomic(struct A) a;
int *p = &a.x;
int f(void) { return 0 && a.x; }
