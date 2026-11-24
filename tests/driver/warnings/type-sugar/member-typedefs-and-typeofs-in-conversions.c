typedef long L;
struct S { typeof(long) m; L l; typeof(L *) p; } s;
int x;
typeof(x + 1) y;
typeof(L *) z;
void f(void)
{
    char *a = &s.m;
    char *b = &s.l;
    char *c = s.p;
    char *d = &y;
    char *e = z;
}
