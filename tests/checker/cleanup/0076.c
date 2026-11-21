static void c(_Atomic long p) { }
void g(void) { int a __attribute__((cleanup(c))); }
