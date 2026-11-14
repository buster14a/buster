void f();
void g(void) {
int a __attribute__((cleanup(f)));
}
