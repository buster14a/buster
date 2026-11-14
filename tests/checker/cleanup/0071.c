void f(double *p);
void g(void) {
__auto_type a __attribute__((cleanup(f))) = 1;
}
