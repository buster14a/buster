#define __counted_by(n) __attribute__((counted_by(n)))
struct Inc;
struct B { int n; struct Inc *__counted_by(n) p; };
struct O { struct B a; struct B b; };
typedef struct { int n; struct Inc *__attribute__((counted_by_or_null(n))) p; struct Inc *__attribute__((sized_by(n))) s; } T;
struct Inc *get(void);
void take(struct B);
struct B gb = { 0, 0 };
void f(void)
{
    struct B b = { 1, get() };
    struct B c = { .n = 2, .p = get() };
    struct B e = { 0 };
    struct B d[3] = { { 1 }, { 2, get() }, [2].n = 3 };
    struct O o1 = { { 1 }, .b.n = 2, .a.p = get() };
    struct O o2 = { .b.n = 2, .a = { 1 } };
    struct O o3 = { 1, get(), 2 };
    struct B r[3] = { [0 ... 2].n = 1 };
    T t = { 1 };
    take((struct B){ 1, get() });
    struct B none;
    struct B copy = b;
    (void)c; (void)e; (void)d; (void)o1; (void)o2; (void)o3; (void)r; (void)t; (void)none; (void)copy;
}
struct Inc { int x; };
void g(void) { struct B late = { 1 }; (void)late; }
