__attribute__((address_space(1))) int *p;
__attribute__((address_space(2))) int *r;
void f(void) { r = p; }
