// #3565: what an _Atomic pointer's read still can't convert to, under the same options: another pointer type stays an
// error, a discarded qualifier a warning, and an integer follows -Wint-conversion.
_Atomic(int *) ap;
_Atomic(const int *) cq;
void take(long *, int *, int);

int *reads(void)
{
    long *w = ap;
    int *m = cq;
    int x = ap;
    w = ap;
    m = cq;
    take(ap, 0, 0);
    take(0, cq, 0);
    take(0, 0, ap);
    (void)w;
    (void)m;
    (void)x;
    return cq;
}

long *other(void)
{
    return ap;
}
