int g(void);
void f(void) {
    int y = 0;
    double d = 1;
    __auto_type *p = &y;
    __auto_type **pp = &p;
    __auto_type const *q = &d;
    __auto_type (*fp)(void) = g;
    _Static_assert(_Generic(p, int *: 1, default: 0), "");
    _Static_assert(_Generic(pp, int **: 1, default: 0), "");
    _Static_assert(_Generic(q, const double *: 1, default: 0), "");
    _Static_assert(_Generic(fp, int (*)(void): 1, default: 0), "");
}
