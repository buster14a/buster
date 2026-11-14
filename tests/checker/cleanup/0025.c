void f(int *p);
void g(void) { int h(void) __attribute__((cleanup(f))); }
