long g(long);
void f(int *p) { long *l = g(p); long *m = __builtin_expect(p, 0); (void)l; (void)m; }
