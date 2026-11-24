enum E { A }; enum F { B };
enum F hf(void);
struct S { enum F f; enum F b : 3; } s, *ps, arr[2];
void f(enum E e, enum F g, int c) {
    enum F g2 = B;
    e = (g2 = g);
    e = (g2 += 1);
    e = g++;
    e = ++g;
    e = (enum F)c;
    e = hf();
    e = ps->f;
    e = (ps)->b;
    e = arr[c].f;
    e = (c, (g2 = g));
    e = __builtin_choose_expr(1, g, e);
    e = _Generic(c, int: (c, g), default: B);
}
