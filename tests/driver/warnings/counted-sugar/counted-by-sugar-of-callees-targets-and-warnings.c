int printf(const char *, ...);
struct B { int n; int *p __attribute__((counted_by(n))); char *q __attribute__((sized_by(n))); };
struct I { int n; struct Inc *r __attribute__((counted_by(n))); };
void g(struct B *b, struct B v, struct I *i, long *l)
{
    b->p();
    b->q / 2;
    &b->p + 1.0;
    b->p * b->p;
    v.p.x;
    b->p % 2;
    ({ b->p; }) + 1.0;
    l = ({ b->p; });
    i->r + 1;
    struct B x = { .p = 2.5 };
}
void h(struct B *b)
{
    b->p = 1 - 1;
    int r = b->p < 1;
    printf("%d %s\n", b->p, b->q);
    struct B y = { .p = 1 ? 0 : 0 };
    (void)(b->n ? b->p : (char *)0);
}
