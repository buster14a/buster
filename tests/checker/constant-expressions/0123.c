enum { N=1 }; struct S { int a[3]; }; int f(void) { enum { N=2 }; return __builtin_offsetof(struct S,a[N]); } _Static_assert(__builtin_offsetof(struct S,a[N])==4, "outer enum");
