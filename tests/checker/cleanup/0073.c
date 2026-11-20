static void c(_Atomic(void *) p) { }
void g(void) { double a __attribute__((cleanup(c))); }
