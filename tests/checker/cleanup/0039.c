void f(int *p);
void (*h)(int *) = f;
void g(void) {
int a __attribute__((cleanup(h)));
}
