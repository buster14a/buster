void f(int *p);
void g(int n) { int a __attribute__((cleanup(f))); void *t[] = { &&l }; goto *t[0]; l:; }
