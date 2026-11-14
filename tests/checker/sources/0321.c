int f(int n) { int s = 0;
#pragma nounroll 4
#pragma unroll 4
 while (n--) s += 1;
#pragma unroll (4) x
#pragma unroll 8
 while (n--) s += 1;
#pragma unroll 4)
 while (n--) s += 1; return s; }
