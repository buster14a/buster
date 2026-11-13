int f(int n) { int s = 0;
#pragma clang loop unroll_count(4uwb) interleave_count(2wb)
 while (n--) s += 1; return s; }
