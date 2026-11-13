int f(int n) { int s = 0;
#pragma unroll typeof(1)
 while (n--) s += 1; return s; }
