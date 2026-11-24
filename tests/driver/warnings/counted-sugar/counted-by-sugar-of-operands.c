struct B { int n; int *p __attribute__((counted_by(n))); char *q __attribute__((sized_by(n))); int fam[] __attribute__((counted_by(n))); };
void f(struct B *b, struct B v, int k)
{
    (b->p) + 1.0;
    b->fam + 1.0;
    (0, b->p) * 2;
    (k ? b->p : b->p) + 1.0;
    -b->p;
    ~b->p;
    b->fam.x;
    b->p.x;
    b->p++ + 1.0;
    *&b->p + 1.0;
    b->fam = 0;
    b->fam++;
    sizeof(b->fam);
    v.p * 2;
    b->p - (char *)0;
    b->p - b->q;
    k ? b->p : 1.5;
    switch (b->p) { }
}
