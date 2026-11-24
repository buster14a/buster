#define __counted_by(n) __attribute__((counted_by(n)))
struct B { int n; int *__counted_by(n) p; int *__counted_by(n) p2, *plain; };
struct F { int n; int fam[] __counted_by(n); };
void take(long *x);
void f(struct B *b, struct F *g, struct B v, long *l)
{
    b->p = l;
    v.p = l;
    take(b->p);
    take(g->fam);
    l = b->p2;
    l = b->plain;
    l = v.p;
    l = &g->fam;
    int *q = b->p;
}
