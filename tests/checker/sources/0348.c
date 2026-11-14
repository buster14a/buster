enum { N = 4 };
int f(int n) { int s = 0;
#pragma clang loop unroll(enable)
#pragma unroll N
 while (n--) s += 1; return s; }
