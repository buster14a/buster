struct S { int x; };
void f(struct S *p);
void g(void) { struct S s __attribute__((cleanup(f))); }
