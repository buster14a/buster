void f(int *p);
void g(void) {
__attribute__((cleanup(f, f))) int h(void), a;
}
