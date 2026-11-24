__attribute__((address_space(1))) int a;
void f(void) {
  __auto_type b = a;
  int *p = &b;
  __auto_type c = &a;
  __attribute__((address_space(1))) int *q = c;
}
