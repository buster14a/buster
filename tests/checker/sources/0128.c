int a[3];
void f(void) {
    __auto_type x = 1, y = 2;
    const __auto_type c = 3, e = 4;
    __auto_type r = a, s = &a[1];
    _Static_assert(_Generic(y, int: 1, default: 0), "");
    _Static_assert(_Generic(&e, const int *: 1, default: 0), "");
    _Static_assert(_Generic(s, int *: 1, default: 0), "");
    (void)x; (void)c; (void)r;
}
