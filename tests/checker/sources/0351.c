enum { N = 4 };
int f(int n) {
#pragma unroll N
#pragma GCC diagnostic push
#pragma unroll 4
    while (n--) {}
    return n;
}
