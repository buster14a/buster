void g(struct S { int x; } s);
struct S { int x; } t;
int f(void) { g(t);
 return 0; }
