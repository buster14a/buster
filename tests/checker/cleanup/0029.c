void f(int *);
void g(void) { int a __attribute__((cleanup(missing), cleanup(f))); }
