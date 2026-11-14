int f(int n) { int s = 0;
#pragma clang loop unroll_count(return)
 while (n--) s += 1; return s; }
