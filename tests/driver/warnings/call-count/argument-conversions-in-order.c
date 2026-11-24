struct S { int a; };
void g(int *p, int (*fp)(int), int x, struct S s, char *c);
void f(long *l, struct S s, unsigned char *u) { g(l, 0, 1, s, u); }
void h(int a, char *c);
void f2(unsigned char *u) { h(u, u); }
void k(int *p, char *c, struct S s);
int m(char *c);
void f3(long *l, unsigned char *u, struct S s) { k(l, u, 1); k(l, (char *)l, s); m(l); int x = m(u) + m(l); (void)x; }
