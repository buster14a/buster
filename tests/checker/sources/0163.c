void g(struct S *p);
struct S { int x; };
int f(void) { struct S s = { 1 }; g(&s); return s.x; }
