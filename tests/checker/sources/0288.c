int f(int* p, int n) { int s = 0;
#pragma clang loop vectorize_width(4) unroll_count(8) interleave_count(2)
 while (n--) s += 1;
#pragma clang loop vectorize_width(4, scalable) pipeline(disable)
 while (n--) s += 1;
#pragma clang loop vectorize_width(fixed) vectorize_predicate(enable) distribute(enable) pipeline_initiation_interval(3)
 while (n--) s += 1;
#pragma clang loop unroll_count(0x10) vectorize_predicate(enable) vectorize(disable)
 while (n--) s += 1;
#pragma clang loop unroll(disable) vectorize(disable),
 while (n--) s += 1; return s; }
