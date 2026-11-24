#define __counted_by(n) __attribute__((counted_by(n)))
struct Inc;
struct B { int n; struct Inc *__counted_by(n) p; };
struct Inc *g(struct B *b, struct Inc *x)
{
    b->p = x;
    struct Inc *y = b->p;
    return b->p;
}
void h(struct B *b) { (void)b->p; void *v = b->p; struct Inc **pp = &b->p; }
