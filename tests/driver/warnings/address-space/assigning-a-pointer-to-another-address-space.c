__attribute__((address_space(1))) int *p;
int *q;
void f(void) { q = p; }
