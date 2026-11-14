typedef int T;
void f(int *);
void g(void) { int a __attribute__((cleanup(T), cleanup(f))); }
