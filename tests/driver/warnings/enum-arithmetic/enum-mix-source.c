enum E { A, B, A2 = A + 1 }; enum F { C, D, D2 = A | C };
typedef enum { T1, T2 } Tn; enum { U1, U2 }; enum { V1 };
#define MIX(a, b) ((a) + (b))
int g(int);
int f(enum E e, enum F f2, Tn t, double d, float _Complex z, unsigned u, long double ld) {
    int r = 0;
    r += (e + f2) < (A | C);
    r += MIX(e, f2);
    r += sizeof(e * f2);
    r += e < u;
    r += t + e;
    r += U1 + V1;
    r += U1 + e;
    r += e + d;
    r += d * e;
    r += z + e;
    r += e == d;
    d += e;
    d -= A;
    r += (e ? d : B) > 0;
    r += (int)e + f2;
    r += (enum E)f2 + e;
    r += g(e - f2);
    r += e % (f2 ^ A) / C;
    if (0) r += e & f2;
    return r;
    r += e - f2;
}
int h(enum E e, enum F f2) { return e <= f2 && e != C; }
