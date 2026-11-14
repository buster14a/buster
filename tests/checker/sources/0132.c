_Alignas(2) __auto_type g = (short)1;
void f(void) {
    _Alignas(1) __auto_type c = (char)0;
    _Alignas(double) __auto_type d = (char)1;
    __auto_type e __attribute__((aligned(1))) = 1.0;
    (void)c; (void)d; (void)e;
}
