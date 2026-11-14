void f(double *p);
void g(void) {
__attribute__((cleanup(f))) __auto_type a = 1.0;
}
