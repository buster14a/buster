void f(int *p);
void h(long *p);
void g(void) { int a __attribute__((cleanup(h), cleanup(f))); }
