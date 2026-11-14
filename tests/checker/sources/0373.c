int f(int* p, int n) { int s = 0;
#pragma clang loop pipeline(disable) pipeline_initiation_interval(3)
 while (n--) s += 1; return s; }
