void f(char **p);
void g(void) { char *a __attribute__((cleanup(f))) = 0; }
