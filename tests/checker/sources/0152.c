void f(void) {
    constexpr double d =
        (1, 0.75);
    constexpr double a[1] =
        { (1, 0.75) };
    (void)d;
    (void)a;
}
