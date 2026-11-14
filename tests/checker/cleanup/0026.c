void f(int *p);
void g(int a __attribute__((cleanup(f)))) { }
