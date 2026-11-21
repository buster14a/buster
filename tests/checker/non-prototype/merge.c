// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
long a();
long a(long x) { return x; }
long b();
long b(int x);
long c(int x);
long c();
long d();
long d(void);
long e();
long e(x) long x; { return x; }
long f(int x);
long f(x) int x; { return x; }
static long g(long x, long y) { return x + y; }
long h();
extern __typeof(g) h;
long i(), j();
long i(int x) { return x; }
int seq(int); int seq(); int seq(int); int seq();
int abs(); int abs(int);
