int f(int* p, int n) { int s = 0;
#pragma clang loop interleave_count(1.5)
 while (n--) s += 1; return s; }
