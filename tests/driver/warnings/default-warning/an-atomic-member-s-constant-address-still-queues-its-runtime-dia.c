struct A { int x; };
_Atomic(struct A) a;
int *p = &a.x;
