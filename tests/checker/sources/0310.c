int f(int* p, int n) { int s = 0;
#pragma clang loop distribute(enable)
#pragma clang loop distribute(disable)
 while (n--) s += 1; return s; }
