int f(int n) { int s = 0;
#pragma clang loop unroll_count(;)
 while (n--) s += 1; return s; }
