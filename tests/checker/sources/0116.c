unsigned long f(void) {
    __auto_type x =
        sizeof(__typeof__(x));
    return x;
}
