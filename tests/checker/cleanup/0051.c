void f(void **p);
void g(void) {
int *a __attribute__((cleanup(f)));
}
