typedef long L;
typedef int I;
int x, arr[4];
struct T { int f; } t;
typeof(x + 1) y;
typeof(L *) z;
typeof(x+1) *w;
typeof(arr[1]) v;
typeof(t.f) u;
typeof(-x) n;
typeof((x)) pq;
typeof(sizeof x) sz;
typeof(L const *) cz;
typeof(I *) iz[2];
typeof(x ? 1 : 2) cond;
void f(void)
{
    char *a = &y;
    char *b = z;
    char *c = w;
    char *d = &v;
    char *e = &u;
    char *g = &n;
    char *h = &pq;
    char *i = &sz;
    char *j = cz;
    char *k = iz;
    char *l = &cond;
}
