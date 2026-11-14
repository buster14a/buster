void f(void);
void g(void) {
int a __attribute__((cleanup(f)));
}
