struct S { int a; };
double f1(double x) { return __builtin_align_up(x, 4); }
int f2(int x) { return __builtin_align_up(x, 4.0); }
int f3(struct S s) { return __builtin_is_aligned(s, 4); }
int f4(int x, int *p) { return __builtin_align_up(x, p); }
void f5(void (*fp)(void)) { (void)__builtin_align_up(fp, 4); }
