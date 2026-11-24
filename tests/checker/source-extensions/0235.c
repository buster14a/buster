struct S { unsigned x:3; }; void f(struct S *p) { &p->x; }
