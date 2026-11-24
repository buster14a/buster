typedef int wchar_t;
typedef unsigned long size_t;
typedef int myint;
typedef signed char tiny;
int s1(wchar_t wc) { return wc < 0xffU; }
int s2(int n) { int a[4]; return n >= sizeof a / sizeof a[0]; }
int s3(int n, size_t m) { return n < m; }
int s4(myint n, unsigned u) { return n < u; }
int s5(int n, size_t m) { return n < m + 1; }
int s6(int n, size_t m) { return n < 1 + m; }
int s7(int n, size_t m) { return n < (m >> 1); }
int s8(int n, size_t m, unsigned long u) { return n < m + u; }
int s9(int n) { return n < (size_t)1; }
int s10(tiny x, tiny y, unsigned u) { return x + y < u; }
