__attribute__((address_space(1))) int a;
int *f(void) { return &a; }
