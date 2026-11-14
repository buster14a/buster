void f(int *p);
void k(long *p);
void g(void) {
__attribute__((cleanup(f), cleanup(k))) int h(void), a;
}
