int f(int n) { int s = 0;
#pragma clang loop vectorize_width()
 while (n--) s += 1; return s; }
