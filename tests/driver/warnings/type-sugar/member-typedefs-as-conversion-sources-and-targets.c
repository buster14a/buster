typedef long L;
typedef L *LP;
typedef int I;
struct In { L deep; };
struct S { L l; LP lp; LP *lpp; struct In in; L arr[3]; typeof(I) ti; I *ip; const L cl; } s, *ps;
void take(char *);
void f(void)
{
    char *a = &ps->l;
    char *b = s.lp;
    char *c = *s.lpp;
    char *d = &s.in.deep;
    char *e = s.arr;
    char *g = &s.arr[1];
    char *h = &s.ti;
    char *k = &s.cl;
    take(&s.l);
    take(s.lp);
    char *m;
    m = s.ip;
    s.lp = m;
    s.ip = m;
    ps->lp = m;
    s.l = m;
    return;
}
