struct S { char c[3]; };
int f(struct S* s, int (*a)[3], long double* d)
{
    struct S r, v = { { 1, 2, 3 } };
    int x[3], y[3] = { 1, 2, 3 };
    long double e = 1.0L, n = 2.0L;
    __atomic_load(s, &r, __ATOMIC_SEQ_CST);
    __atomic_store(a, &y, __ATOMIC_RELEASE);
    __atomic_exchange(s, &v, &r, __ATOMIC_ACQ_REL);
    __atomic_load(a, &x, __ATOMIC_ACQUIRE);
    return __atomic_compare_exchange(d, &e, &n, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) + r.c[0] + x[1];
}
