__attribute__((address_space(1))) int *p;
int *q;
void f(int c) { void *v = c ? p : q; }
