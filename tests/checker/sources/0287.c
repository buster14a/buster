int f(int* p, int n) { int s = 0;
#pragma clang loop vectorize(disable) interleave(disable)
 for (int i = 0; i < n; i += 1) s += p[i];
#pragma clang loop unroll(disable)
 while (n--) s += 1;
#pragma clang loop unroll(full)
 do s += 1; while (s < 9);
 if (n)
#pragma clang loop vectorize(assume_safety) interleave(assume_safety)
 while (n--) s += 1;
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
 for (;;) break;
 return s; }
