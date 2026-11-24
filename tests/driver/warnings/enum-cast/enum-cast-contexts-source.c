enum E { A, B }; enum F { C, D }; typedef enum E TE; enum { U1 }; typedef enum { T1 } Tn;
struct S { enum E e; enum F f : 2; };
enum E g(enum E);
enum E h(enum F f) {
    enum E e = C;
    enum E e2 = (enum F)0;
    enum F f2 = f;
    e = f;
    e = (f);
    e = 0 ? f : f;
    e = (e, f);
    TE t = f;
    struct S s = { f, e };
    s.e = f;
    g(f);
    e = U1;
    enum E e3 = (enum { V1 })0;
    Tn tn = f;
    e += f;
    e = (enum E)f;
    e = (int)f;
    _Bool bb = f;
    int i = f;
    (void)e2; (void)f2; (void)t; (void)e3; (void)tn; (void)bb; (void)i;
    if (0) e = f;
    return f;
}
