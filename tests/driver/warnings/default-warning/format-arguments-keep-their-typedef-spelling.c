typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef int wchar_t;
typedef int myint;
int printf(const char*,...);
void f(size_t m, ptrdiff_t d, wchar_t *w, myint n) { printf("%u", m); printf("%u", d); printf("%d", w); printf("%lu", -n); printf("%lu", +n); printf("%lu", ~n); }
