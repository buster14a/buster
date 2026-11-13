int f(int n) { int s = 0;
#pragma clang loop pipeline_initiation_interval()
 while (n--) s += 1; return s; }
