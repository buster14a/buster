__attribute__((const)) int c(int);
__attribute__((pure)) int p(int);
__attribute__((warn_unused_result)) int w(int);
[[nodiscard]] int d(int);
[[nodiscard("because")]] int dm(int);
__attribute__((warn_unused_result("why"))) int wm(int);
[[gnu::warn_unused_result]] int gw(int);
__attribute__((const, warn_unused_result)) int cw(int);
int e(int);
int g(int n) {
  c(n); p(n); w(n); d(n); dm(n); wm(n); gw(n); cw(n); e(n);
  (c)(n); (void)w(n); (void)c(n);
  c(n), 0; n, c(n); c(n), p(n);
  n ? c(n) : p(n); n ? c(n) : e(n); n ? e(n) : p(n); n ? c(n) : 0;
  for (c(n); n; p(n)) break;
  if (n) c(n); else w(n);
  return c(n), w(n);
}
