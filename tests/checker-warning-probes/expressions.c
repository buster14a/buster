int fn(void);
_Bool bf(void);
void vf(void);
int pf(int);
int *ptrfn(void);
int weakfn(void) __attribute__((weak));
extern int array[3];
extern int weakarray[3] __attribute__((weak));
struct Pair { int x; int a[3]; int *p; };
#define SELF(x) ((x) == (x))
#define FUN fn
#define BOOL(x) (!!(x))
#define CMP(x, y) ((x) == (y))
int comparisons(int x, volatile int v, _Atomic int at, double d, int *p, int i, struct Pair *s, struct Pair t) {
    int r = x == x;
    r += x != x;
    r += x < x;
    r += x >= x;
    r += (x) <= (x);
    r += (int)x == (int)x;
    r += v == v;
    r += at == at;
    r += d == d;
    r += d != d;
    r += p == p;
    r += *p == *p;
    r += s->x == s->x;
    r += t.x == t.x;
    r += s->x == t.x;
    r += p[0] == p[0];
    r += p[i] == p[i];
    r += p[i+1] == p[i+1];
    r += array == array;
    r += array == weakarray;
    r += fn == fn;
    r += fn() == fn();
    r += SELF(x);
    r += CMP(x,x);
    if (0) r += x == x;
    r += 0 && (x == x);
    r += sizeof(x == x);
    int b[(x == x) + 2];
    (void)b;
    r += _Generic(x, int: x == x, default: x == x);
    r += __builtin_choose_expr(1, x == x, x == x);
    return r;
    r += x == x;
}
int pointers(int x, int *p, struct Pair *s, struct Pair t) {
    int r = 0;
    if (fn) r++;
    if (bf) r++;
    if (vf) r++;
    if (pf) r++;
    if (weakfn) r++;
    if (&fn) r++;
    if ((fn)) r++;
    if (array) r++;
    if (weakarray) r++;
    if (&p) r++;
    if (&(p)) r++;
    if ((&p)) r++;
    if (&s->x) r++;
    if (s->a) r++;
    if (t.a) r++;
    r += !fn;
    r += !(&p);
    r += fn && bf;
    r += array ? x : 0;
    _Bool b = fn;
    b = fn;
    b = (_Bool)fn;
    b = (_Bool)&p;
    (void)b;
    r += fn == 0;
    r += 0 != fn;
    r += ptrfn == 0;
    r += fn == (void *)0;
    r += &fn == 0;
    r += (fn) == 0;
    r += &p == 0;
    r += 0 != &p;
    r += (&p) == 0;
    r += &(p) == 0;
    r += array == 0;
    r += weakarray == 0;
    r += &s->x == 0;
    r += s->a == 0;
    if (FUN) r++;
    r += BOOL(fn);
    r += CMP(fn, 0);
    r += sizeof(fn == 0);
    r += sizeof(!fn);
    if (0) r += fn == 0;
    if (0) { if (fn) r++; }
    return r;
    if (fn) r++;
}
int strings(const char *s, int x) {
    int r = s == "x";
    r += "x" != s;
    r += s < "x";
    r += (char *)"x" == s;
    r += ("x") == s;
    r += "x" == "x";
    r += "x" == "y";
    r += "x" == 0;
    r += "x" != (void *)0;
    r += "x" == (char *)0;
    r += "x" == (1 - 1);
    r += sizeof(s == "x");
    if (0) r += s == "x";
    r += 0 && (s == "x");
    r += _Generic(x, int: s == "x", default: s == "x");
    return r;
    r += s == "x";
}
