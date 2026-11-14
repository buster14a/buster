#pragma clang loop unroll(enable) 4
#pragma nounroll 4
#pragma unroll (4) x
int f(int n) { int s = 0;
#pragma clang loop unroll(enable) 4
#pragma nounroll 4
#pragma unroll (4) x
 s += 1; return s; }
