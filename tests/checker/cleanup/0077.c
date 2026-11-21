struct S { int x; };
static void c(_Atomic(struct S *) p) { }
void g(void) { int a __attribute__((cleanup(c))); }
