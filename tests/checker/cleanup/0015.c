void f(long *p);
void g(void) {
__attribute__((cleanup(f))) int h(void), a;
}
