enum { N = 4 };
int f(int n) { int s = 0;
#pragma clang loop vectorize(disable) vectorize_width(N)
 while (n--) s += 1; return s; }
