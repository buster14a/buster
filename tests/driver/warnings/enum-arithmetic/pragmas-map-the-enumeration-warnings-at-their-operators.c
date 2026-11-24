enum E { A, B }; enum F { C, D };
int f(enum E e, enum F g) {
    int r = e < g;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wenum-compare"
    r += e == g;
#pragma clang diagnostic warning "-Wenum-enum-conversion"
    r += e + g;
#pragma clang diagnostic pop
    r += e != C;
    r += e + g;
    return r;
}
