int f(int n) { int s = 0;
#pragma clang loop unroll_count(0) 4
#pragma clang loop unroll_count() 4
#pragma clang loop unroll_count(4))
#pragma clang loop unroll(disable)
 while (n--) s += 1; return s; }
