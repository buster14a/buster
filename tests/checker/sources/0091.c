struct S { int a; };
void f(void) { struct S __attribute__((unused)); struct S s; s.a = 1; }
enum K : int; enum K __attribute__((unused)); void g(void) { enum K __attribute__((unused)); enum K k = 0; (void)k; }
