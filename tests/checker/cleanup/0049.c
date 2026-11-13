void f(int *p);
void g(void) {
unsigned a __attribute__((cleanup(f)));
}
