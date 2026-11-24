int a(int x) { return __builtin_align_up(x, 1); }
int b(int x) { return __builtin_align_down(x, 1); }
int c(int x) { const int k = 1; return __builtin_is_aligned(x, k); }
int d(int x) { return __builtin_is_aligned(x, (char)1) + sizeof(__builtin_is_aligned(x, 1)); }
#pragma clang diagnostic ignored "-Wtautological-compare"
int e(int x) { return __builtin_is_aligned(x, 1); }
