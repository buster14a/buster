int f(int n) { int s = 0;
#pragma clang loop unroll_count(0x10000000000000002uwb)
 while (n--) s += 1; return s; }
