int f(int n) { int s = 0;
#pragma clang loop licm(enable)
 while (n--) s += 1; return s; }
