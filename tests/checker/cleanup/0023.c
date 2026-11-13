void f(int *p);
void g(void) { extern int a __attribute__((cleanup(f))); }
