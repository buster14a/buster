int g(struct T { int x; } a, struct T { int x; } b, struct T *c) { return a.x + b.x + c->x; }
int n(struct W { int a; } *p, int (*q)(struct W { int a; } *r)) { return p->a + (q != 0); }
