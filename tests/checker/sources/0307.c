int f(int n) { int s = 0;
#pragma clang loop interleave_count(0x1000000000000000000000000000000002uwb)
 while (n--) s += 1; return s; }
