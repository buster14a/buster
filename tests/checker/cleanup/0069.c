void f(void *p);
void g(void) {
__auto_type a __attribute__((cleanup(f))) = (struct { int x; }){ 1 };
}
