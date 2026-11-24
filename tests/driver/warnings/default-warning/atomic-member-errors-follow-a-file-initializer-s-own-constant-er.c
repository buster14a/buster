struct A { int x; };
_Atomic(struct A) a;
int use = a.x;
