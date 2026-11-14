void f(int *p);
void g(void) { register int a __attribute__((cleanup(f))) = 1; }
