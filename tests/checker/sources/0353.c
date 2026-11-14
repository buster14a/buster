void f(int n) {
#pragma clang loop unroll_count(4 / 0)
#pragma clang loop unroll(full)
    while (n--) {}
}
