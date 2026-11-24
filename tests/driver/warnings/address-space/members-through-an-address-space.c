struct S { int m; };
__attribute__((address_space(1))) struct S s;
int *f(void) { return &s.m; }
