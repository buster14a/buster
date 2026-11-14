void f(int *p);
void g(void) { static int a __attribute__((cleanup(f))); }
