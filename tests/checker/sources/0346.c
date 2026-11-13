int N = 4;
void f(int n) {
#pragma clang loop unroll_count(N)
    n++;
}
