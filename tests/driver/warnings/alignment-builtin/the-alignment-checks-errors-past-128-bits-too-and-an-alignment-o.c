int f(int x) { return __builtin_align_up(x, 0); }
int g(int x) { return __builtin_align_up(x, 1L << 40); }
int h(int x) { return __builtin_align_up(x, 3); }
_BitInt(200) k(_BitInt(200) x) { return __builtin_align_up(x, (unsigned _BitInt(300))1 << 200); }
int l(int x) { return __builtin_is_aligned(x, 1); }
