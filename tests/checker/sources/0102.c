void f(int n) {
    void *p = &&here;
    goto *n;
here:
    (void)p;
}
