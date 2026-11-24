int e(int);
__attribute__((warn_unused_result)) int w(int);
__attribute__((pure)) int q(int);
int a, x, m;
void f(int n) {
  (a, n ? e(n) : w(n)), x; x, (a, n ? e(n) : w(n));
  n ? w(n) : (x, w(n)); n ? (x, w(n)) : w(n); n ? w(n) : (m ? w(n) : w(n)); n ? w(n) : _Generic(n, int: w(n));
  n ? w(n) : (x, q(n)); n ? q(n) : _Generic(n, int: q(n));
  _Generic(n, int: (x, w(n)), default: 0); __builtin_choose_expr(1, (x, w(n)), 0); _Generic(n, int: (n ? w(n) : w(n)), default: 0);
}
int g(int n) {
  if (n) { return 0; n ? w(n) : w(n); n, w(n); (n ? w(n) : w(n)), 0; w(n), 0; _Generic(n, int: w(n)); (n, w(n)), n; }
  for (;; n ? w(n) : w(n)) break;
  if (0) { n ? w(n) : w(n); }
  return 1 ? 0 : (w(n), 1);
}
int h(int n) { return 0 && (w(n), 1); }
void k(int n) {
  (w(n)); x = (w(n), 5); (w)(n); ((w))(n); ((w)(n)); (*w)(n); (**w)(n); (&w)(n);
}
