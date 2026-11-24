struct S { unsigned x:3; }; int f(struct S *p) { p->x+=8; return p->x++; }
