static void c(const _Atomic(const int *) p) { }
void g(void) { const int a __attribute__((cleanup(c))) = 1; int b __attribute__((cleanup(c))); }
