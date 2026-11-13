int f(void) {
    void *p = &&inner;
    goto *p;
    return ({ inner: 1; });
}
