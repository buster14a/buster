int f(int n) { int s = 0;
#pragma unroll sizeof(int)
 while (n--) s += 1; return s; }
