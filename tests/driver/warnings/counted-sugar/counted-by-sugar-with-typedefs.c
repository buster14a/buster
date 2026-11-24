typedef int T;
struct B { int n; const int *__attribute__((counted_by(n + 1))) e; T __attribute__((aligned(8))) *t; __attribute__((aligned(8))) T *u; const T *__attribute__((counted_by(n))) v; };
void f(struct B *b, long *l)
{
    l = b->e;
    l = b->t;
    l = b->u;
    l = b->v;
}
