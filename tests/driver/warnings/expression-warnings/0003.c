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
