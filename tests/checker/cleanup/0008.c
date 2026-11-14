typedef void F(int *);
F f;
void g(void) { int a __attribute__((cleanup(f))); }
