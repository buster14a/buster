// Unknown attributes and members (#2376): an unknown attribute's name with its scope, and a member access's base.
__attribute__((foo)) int a;
[[gnu::bar]] int b;
[[vendor::baz]] int c;
struct T { int m; } t;
int d(void) { return t.nope; }
int e(void) { return (t).nope; }
int f(int x) { return x.m; }
int *g(void) { long *p = 0; int *q = p; return q; }
