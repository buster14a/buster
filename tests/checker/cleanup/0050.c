void f(char *p);
void g(void) {
int a __attribute__((cleanup(f)));
}
