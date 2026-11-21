static void c(_Atomic(int *) p) { }
void g(void) { int a __attribute__((cleanup(c))); }
