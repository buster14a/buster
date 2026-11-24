void g(int *p, int (*fp)(int));
void f(long *l) { g(l, l); }
