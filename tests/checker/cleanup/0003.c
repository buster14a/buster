void f(int *p);
void g(void) { const int a __attribute__((cleanup(f))) = 1; }
