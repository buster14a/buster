int f(int* p, int n) { int s = 0;
#pragma clang loop vectorise(enable)
 for (int i = 0; i < n; i += 1) s += p[i]; return s; }
