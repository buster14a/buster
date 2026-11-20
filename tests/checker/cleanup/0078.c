typedef _Atomic(int *) A;
static void c(A p) { }
void g(void) { __auto_type a __attribute__((cleanup(c))) = 1; __auto_type b __attribute__((cleanup(c))) = 1.0; }
