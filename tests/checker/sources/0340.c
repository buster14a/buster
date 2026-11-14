int f(int n) { int s = 0;
#pragma clang loop licm(disable) licm(disable)
 while (n--) s += 1; return s; }
