int f(int n) { int s = 0;
#pragma clang loop vectorize(enable x) unroll(enable, x)
 while (n--) s += 1;
#pragma clang loop vectorize_width(scalable, 4) interleave_count(4, x)
 while (n--) s += 1;
#pragma clang loop vectorize_width(4, scalable x) unroll_count(4 = 5)
 while (n--) s += 1;
#pragma clang loop vectorize_width(4 x, scalable)
 while (n--) s += 1;
#pragma clang loop vectorize_width(fixed, 4)
 while (n--) s += 1; return s; }
