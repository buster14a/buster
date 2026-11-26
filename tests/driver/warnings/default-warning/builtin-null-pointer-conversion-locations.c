void take(long*);

long* result(int* p)
{
    return __builtin_expect(0, p);
}

void check(int* p)
{
    long* a = __builtin_expect(0, p);
    long* b = (__builtin_expect(0, p));
    long* c = __builtin_expect_with_probability(0, p, 0.5);
    long* d = __builtin_ctz(1);
    long* e = __builtin_popcount(0);
    long* f = __builtin_constant_p(p);
    long* g = __builtin_expect(0, 0);
    long* h = __builtin_expect(0, p) + 0;
    long* i = __builtin_expect(0, 0) ? 0 : 0;
    long* j = __builtin_expect(0, p) ? 0 : 0;
    long* k = (__builtin_expect(0, 0) ? 0 : 0);
    a = __builtin_expect(0, p);
    take(__builtin_expect(0, p));
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    (void)i;
    (void)j;
    (void)k;
}
