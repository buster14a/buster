int f(int n) { int s = 0;
#pragma unroll (4 + 4)
 while (n--) s += 1; return s; }
