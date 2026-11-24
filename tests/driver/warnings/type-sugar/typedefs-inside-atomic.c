typedef int I;
typedef long L;
struct S { int x; };
typedef struct S TS;
void f(_Atomic(I) *ap, _Atomic(L) a2[2], _Atomic(TS) *sp, _Atomic I *kw)
{
    ap();
    char *d = ap;
    char *e = a2;
    char *g = sp;
    char *h = kw;
}
