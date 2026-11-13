int f(int* p, int n) { int s = 0;
#pragma clang loop interleave_count(2147483648)
 while (n--) s += 1; return s; }
