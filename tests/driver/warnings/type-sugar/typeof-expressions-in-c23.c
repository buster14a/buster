typedef long L;
int x;
const int cx = 0;
__typeof__(x * 2) a;
__typeof(x) b2;
typeof_unqual(cx + 1) c;
typeof_unqual(L *) d;
typeof(typeof(x + 1)) e;
int f(int);
typeof(f(x)) h;
typeof((L)x) i;
typeof("ab") s;
void k(void)
{
    char *p1 = &a; char *p3 = &c; char *p4 = d; char *p5 = &e; char *p6 = &h; char *p7 = &i; int *p8 = &s;
}
