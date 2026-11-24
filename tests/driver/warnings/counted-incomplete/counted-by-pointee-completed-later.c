#define __counted_by(n) __attribute__((counted_by(n)))
struct Inc;
struct Late;
struct B { int n; struct Inc *__counted_by(n) p; struct Late *__counted_by(n) l; struct Inc *__attribute__((counted_by_or_null(n))) o; struct Inc *__attribute__((sized_by(n))) z; };
void early(struct B *b, struct B v)
{
    struct Late *a = b->l;
    struct Inc *c = v.p;
    struct Inc *d = b->o;
    struct Inc *e = b->z;
    if (b->p) { }
    b->o = 0;
    struct Inc **f = &v.p;
}
struct Late { int x; };
void late(struct B *b)
{
    struct Late *a = b->l;
    int k = b->l[0].x;
}
