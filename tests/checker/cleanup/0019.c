void f(int *p);
void g(void) { for (int a __attribute__((cleanup(f))) = 0; a < 3; a++) { } }
