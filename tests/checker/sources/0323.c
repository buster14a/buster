int f(int n) { int s = 0;
#pragma unroll
#pragma GCC unroll
 while (n--) s += 1; return s; }
