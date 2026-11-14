void f(int *p);
void g(void) { typedef int T __attribute__((cleanup(f))); }
