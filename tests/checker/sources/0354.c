enum { N = 0 };
void f(int n) {
#pragma clang loop unroll_count(N)
#pragma clang loop unroll(full)
    while (n--) {}
}
