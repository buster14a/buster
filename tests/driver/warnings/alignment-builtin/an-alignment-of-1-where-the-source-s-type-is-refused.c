struct S{int a;};enum E{A};
int a(double x) { return __builtin_is_aligned(x, 1); }
int b(int x) { return __builtin_is_aligned(x, 1); }
int c(struct S s) { return __builtin_is_aligned(s, 1); }
int d(enum E e) { return __builtin_is_aligned(e, 1); }
int e(void (*f)(void)) { return __builtin_is_aligned(f, 1); }
int g(int *p) { return __builtin_is_aligned(p, 1) + __builtin_is_aligned(p + 1, 1); }
int h(char a[4]) { char b[3]; return __builtin_is_aligned(b, 1) + __builtin_is_aligned(a, 1); }
