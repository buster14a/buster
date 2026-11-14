int N = 4;
void f(int n) {
#pragma unroll N
    n++;
}
