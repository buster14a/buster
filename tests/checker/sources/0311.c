int f(int* p, int n) { int s = 0;
#pragma clang loop vectorize_width(4, fixed) vectorize_width(8)
 while (n--) s += 1; return s; }
