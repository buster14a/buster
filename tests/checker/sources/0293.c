int f(int* p, int n) { int s = 0;
#pragma clang loop unroll(disable)
 do s += 1; while (--n);
#pragma clang loop unroll(disable)
 return s; }
