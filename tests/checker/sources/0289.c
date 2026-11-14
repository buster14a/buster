int f(int* p, int n) { int s = 0;
#pragma unroll
 while (n--) s += 1;
#pragma unroll 4
 while (n--) s += 1;
#pragma unroll(4)
 while (n--) s += 1;
#pragma nounroll
 while (n--) s += 1;
#pragma unroll_and_jam 2
 while (n--) s += 1;
#pragma nounroll_and_jam
 while (n--) s += 1;
#pragma GCC unroll 4
 while (n--) s += 1;
#pragma unroll 0
 while (n--) s += 1;
#pragma nounroll 4
 while (n--) s += 1; return s; }
