void f(void) {
  static __attribute__((address_space(1))) int s;
  extern __attribute__((address_space(1))) int e;
  __attribute__((address_space(1))) int local;
}
