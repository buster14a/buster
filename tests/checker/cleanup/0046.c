void f(int *p, int q);
void g(void) {
int a __attribute__((cleanup(f)));
}
