enum E { A, B }; enum F { C, D }; typedef enum E TE; typedef enum F TF; typedef enum { T1 } Tn;
enum E ge = C;
TE gt = D;
enum E garr[2] = { C, A };
struct S { enum E e; enum F f; } gs = { C, A };
void take(enum E, enum F);
TE back(enum F f) { return f; }
Tn anon(enum F f) { return f; }
int f(enum E e, TF g, int c) {
    e = c ? (c ? g : C) : D;
    e = (0, 1, g);
    take(g, e);
    take(C, A);
    struct S s = { g, e };
    struct S *p = &(struct S){ g, e };
    enum E arr[] = { g, C, e };
    TE t = g;
    t = C;
    e = (enum F)c;
    e = (enum E)g;
    e += g;
    c = g;
    (void)s; (void)p; (void)arr; (void)t;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-enum-enum-cast"
    e = g;
#pragma clang diagnostic pop
    if (0) e = g;
    return e = g;
}
