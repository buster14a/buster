enum E { A, B };
int f1(int x) { return __builtin_align_up(x, (0)); }
int f2(int x) { return __builtin_align_down(x, -1); }
int f3(int x) { return __builtin_align_up(x, (long)6); }
char f4(char x) { return __builtin_align_up(x, 256); }
char *f5(char *x) { return __builtin_align_up(x, (__int128)1 << 64); }
enum E f6(enum E x) { return __builtin_align_up(x, 3); }
_Bool f7(unsigned short x) { return __builtin_is_aligned(x, 1 << 17); }
int f8(int x) { const int a = 0; return __builtin_align_up(x, a); }
static const int k = 5;
int f9(int x) { return __builtin_align_up(x, k); }
unsigned _BitInt(130) g(unsigned _BitInt(130) x) { return __builtin_align_up(x, (unsigned _BitInt(140))1 << 131); }
int f10(int x) { return __builtin_align_up(x, sizeof(int[3])); }
