int f(int n) { int s = 0;
#pragma clang loop licm(disable) vectorize_predicate(assume_safety)
 while (n--) s += 1; return s; }
