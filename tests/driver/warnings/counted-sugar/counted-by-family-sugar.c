#define __counted_by(n) __attribute__((counted_by(n)))
struct B { int n; int *__counted_by(n) p; char *__attribute__((sized_by(n))) q; void *__attribute__((sized_by_or_null(n))) r; int *__attribute__((counted_by_or_null(n))) s; };
struct F { int n; int fam[] __counted_by(n); };
void f(struct B *b, struct F *g)
{
    long *l;
    l = b->p;
    l = b->q;
    l = b->r;
    l = b->s;
    l = g->fam;
    l = &b->p;
    double d = b->p;
    int *ok = b->p;
}
