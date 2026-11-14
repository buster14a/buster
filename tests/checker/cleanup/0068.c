void f(char **p);
void g(void) {
char b[4];
__auto_type a __attribute__((cleanup(f))) = b;
}
