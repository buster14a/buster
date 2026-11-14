void f(void) {
    void *p = &&here;
    void *q = &&nowhere;
    goto *p;
here:
    (void)q;
}
