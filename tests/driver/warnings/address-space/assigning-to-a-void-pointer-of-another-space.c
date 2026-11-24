__attribute__((address_space(1))) void *v;
int *q;
void f(void) { v = q; }
