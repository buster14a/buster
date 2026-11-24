int abs(int);
double rint(double);
double sin(double);
static long labs(long x) { return x; }
struct S { int a; };
void f(int n, double d, const char *s) {
  __builtin_expect(n, 1); __builtin_clz(n); __builtin_popcount(n); __builtin_constant_p(n); __builtin_classify_type(n);
  __builtin_abs(n); __builtin_fabs(d); __builtin_rint(d); __builtin_sin(d); __builtin_nan(""); __builtin_isnan(d);
  __builtin_strlen(s); __builtin_bswap32(n);
  abs(n); rint(d); sin(d); labs(n);
  __builtin_offsetof(struct S, a); __builtin_types_compatible_p(int, long);
  _Generic(n, int: n, default: 0); _Generic(n, int: abs(n), default: 0); __builtin_choose_expr(1, abs(n), 0);
}
