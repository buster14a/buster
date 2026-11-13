enum { N = 4 };
int f(int n) {
#pragma clang loop unroll_count(N)
#pragma clang loop unroll(full)
    while (n--) {}
    return n;
}
