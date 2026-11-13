int *nret(void) __attribute__((returns_nonnull));
int *plain(void);
int *nret(void);
void take(int **);
int calls(void) {
    int r = nret() == 0;
    r += 0 != nret();
    if (nret()) r++;
    if ((nret())) r++;
    r += (nret()) == 0;
    r += !nret();
    r += sizeof(nret() == 0);
    r += sizeof(!nret());
    if (0) { if (nret()) r++; }
    r += plain() == 0;
    return r;
}
int params(int *p, int *q, int *r) __attribute__((nonnull));
int params(int *p, int *q, int *r) {
    int v = p == 0;
    v += 0 != p;
    if (p) v++;
    if ((p)) v++;
    v += (p) == 0;
    p = 0;
    if (p) v++;
    v += p == 0;
    q++;
    if (q) v++;
    q += 1;
    if (q) v++;
    (q) = 0;
    if (q) v++;
    v += q == 0;
    take(&r);
    if (r) v++;
    v += r == 0;
    return v;
}
int ordered(int *p, int *q, int *r) __attribute__((nonnull));
int ordered(int *p, int *q, int *r) {
    int v = 0;
    if (p && (p = 0)) v++;
    v += q == 0 || (q = 0);
    if (r) v++;
    take(&(r));
    if (r) v++;
    return v;
}
int annotated(int *p __attribute__((nonnull)), int *q) {
    int r = p == 0;
    if (p) r++;
    if (q) r++;
    return r;
}
int arrays(void) {
    int a[2], b[2];
    int r = a == b;
    r += a != b;
    r += a < b;
    r += a <= b;
    r += a >= b;
    r += a > b;
    return r;
}
int fn(void);
#define ARG(x) x
#define BODY fn
#define TEST(x) (!!(x))
int macros(void) {
    int r = 0;
    if (ARG(fn)) r++;
    if (BODY) r++;
    r += TEST(fn);
    r += ARG(fn) == 0;
    r += fn == ARG(0);
    r += ARG(fn == 0);
    return r;
}
void bargs(_Bool, _Bool);
_Bool boolresult(void) {
    bargs(fn, fn);
    return fn;
}
