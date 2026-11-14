void a(int *p);
void g(void) { int a __attribute__((cleanup(a))); }
