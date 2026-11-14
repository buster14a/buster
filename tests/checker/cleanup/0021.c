void f(int *p);
int a __attribute__((cleanup(f)));
