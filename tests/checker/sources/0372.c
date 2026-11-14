int f(int* p, int n) { int s = 0;
#pragma clang loop unroll(disable)
#pragma clang loop unroll_count(4)
 while (n--) s += 1; return s; }
