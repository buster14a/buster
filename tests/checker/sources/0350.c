enum { N = 4 };
int f(int n) {
#pragma clang loop vectorize_width(N)
#pragma clang loop vectorize(disable)
    while (n--) {}
    return n;
}
