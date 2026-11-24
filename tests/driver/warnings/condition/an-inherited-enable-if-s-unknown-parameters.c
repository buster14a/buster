int a(int n) __attribute__((enable_if(1 || n, "or")));
int a(int n);
int c(int n) __attribute__((enable_if(n || 1, "lor")));
int c(int n);
int d(int n) __attribute__((enable_if(sizeof(n) == 4, "size")));
int d(int n);
int e(int n) __attribute__((enable_if((n, 1), "comma")));
int e(int n);
int f(int n) __attribute__((enable_if(n * 0 == 0, "mul")));
int f(int n);
int p(int *q) __attribute__((enable_if(q != 0, "nonnull")));
int p(int *q);
int g(int n) __attribute__((enable_if(1 || n, "ok"), diagnose_if(n == 8, "eight", "warning")));
int g(int n);
int v;
void t(void) { a(1); c(1); d(1); e(1); f(1); p(&v); g(8); g(7); }
