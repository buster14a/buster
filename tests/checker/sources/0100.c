void f(int n) {
    void *p = &&inner;
    goto *p;
    { int v[n]; inner: v[0] = 1; }
}
