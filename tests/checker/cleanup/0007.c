int f(int *p);
void g(void) { int a __attribute__((cleanup(f))); }
