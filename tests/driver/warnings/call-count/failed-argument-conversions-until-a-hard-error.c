struct S { int a; };
void g(int *p, int (*fp)(int), int x, struct S s, char *c);
void f(long *l, struct S s, unsigned char *u) { g(l, l, 1, s, (char*)u); g(0, 0, s, 1, 0); g(l, 0, 0, s, (char*)0); }
void h(void (*fp)(int *, int *), long *l) { fp(l, l); (*fp)(l, 0); }
void k(int __seg_gs *a, int *b);
void m(int *p, int __seg_fs *q) { k(p, q); k(q, q); }
