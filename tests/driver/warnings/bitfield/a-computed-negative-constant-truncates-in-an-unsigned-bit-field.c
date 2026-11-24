struct S { unsigned u : 3; int b : 3; unsigned v : 1; };
struct S g = { 0 - 2, 0 - 4, 1 };
void f(struct S *p) { struct S t = { .u = 0 - 2, .b = -4, .v = 1 }; p->u = 0 - 2; p->b = 0 - 5; p->v = 1; (void)t; }
