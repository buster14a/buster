void f(void *p) {
    void *q = &&here;
    goto *p ? 1 : 0;
here:
    (void)q;
}
