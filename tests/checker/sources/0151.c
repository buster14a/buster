static int s;
void f(void) {
    constexpr double a[1] =
        { (s, 0.75) };
    (void)a;
}
