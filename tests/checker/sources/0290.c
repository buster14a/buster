int f(int* p, int n) { int s = 0;
#pragma clang loop vectorize(disable)
 s = 1; return s; }
