static void c(_Atomic _Bool p) { }
void g(void) { int a __attribute__((cleanup(c))); }
