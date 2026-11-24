enum E { A, B }; enum F { C, D }; enum { X, Y }; typedef enum E TE;
int f(enum E e, enum F g, TE t, float fl, int i) {
    int r = 0;
    r += e + g;
    r += A + C;
    r += e | g;
    r += e < g;
    r += i ? e : g;
    r += e + X;
    r += e + fl > 0;
    r += t + g;
    r += e + t;
    e += g;
    r += (e, g);
    r += A & D;
    r += e * C;
    r += e == g;
    r += e << g;
    return r;
}
