enum E { A, B }; enum F { C, D }; typedef enum E TN; typedef const enum E CNST; typedef enum { X } AN;
int f(const enum E ce, volatile enum E ve, enum F f2, const double cd, const TN ct, CNST cn, volatile float vf, enum E e, double d, const AN an) {
    int r = 0;
    r += ce < f2; r += f2 == ve; r += ce < cd; r += vf > e; r += ct != f2; r += cn <= f2; r += an == f2;
    r += ce + f2; r += ct | f2; r += cd * ce;
    r += e ?: f2; r += e ?: 1.5; r += e ?: d; r += cd ?: e; r += d ?: e; r += A ?: C;
    return r;
}
