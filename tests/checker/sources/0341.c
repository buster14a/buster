int f(int n) { int s = 0;
#pragma clang loop vectorize_width(4, x)
 while (n--) s += 1; return s; }
