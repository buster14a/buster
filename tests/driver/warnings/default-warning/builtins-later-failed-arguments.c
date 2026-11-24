struct S { int a; } s; int *p; float *fp; double d; char q[4];
void f(void) {
long a = __builtin_expect(p, p); long c = __builtin_expect(p, s); long e = __builtin_expect_with_probability(p, p, p);
void *m = __builtin_memcpy(1, 2, p); void *n = __builtin_memcpy(fp, d, p);
(void)__builtin_alloca_with_align(p, 64); __xray_typedevent(p, q, p);
(void)a; (void)c; (void)e; (void)m; (void)n; }
