void f(const volatile int *p);
void g(void) { int a __attribute__((cleanup(f))); }
