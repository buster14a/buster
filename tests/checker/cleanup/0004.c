void f(void *p);
void g(void) { long a __attribute__((cleanup(f))); }
