void f(void) {
    volatile __auto_type y = 1.0;
    _Static_assert(_Generic(&y, volatile double *: 1, default: 0), "volatile");
    const __auto_type x = 1;
    _Static_assert(_Generic(&x, const int *: 1, default: 0), "const");
}
