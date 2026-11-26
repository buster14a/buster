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
