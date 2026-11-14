int f(int n) { int s = 0;
#pragma unroll_and_jam (4
 while (n--) s += 1; return s; }
