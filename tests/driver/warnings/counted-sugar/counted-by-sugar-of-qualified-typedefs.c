typedef const int CI;
typedef int *IP;
struct B { int n; CI *__attribute__((counted_by(n))) p; volatile CI *__attribute__((counted_by(n))) q; };
void f(struct B *b, long *l) { l = b->p; l = b->q; l = &b->p; }
