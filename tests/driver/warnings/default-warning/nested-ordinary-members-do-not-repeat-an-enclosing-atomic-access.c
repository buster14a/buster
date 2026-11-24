struct B { int x; };
struct A { struct B b; };
_Atomic(struct A) a;
_Atomic(union U { int x; double d; }) u;
int f(void) { return a.b.x + u.x; }
