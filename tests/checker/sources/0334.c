enum { N = 4 };
int f(int n) { int s = 0;
#pragma clang loop unroll_count(N)
 while (n--) s += 1; return s; }
