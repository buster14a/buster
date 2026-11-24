__attribute__((warn_unused_result)) int w(int);
__attribute__((const)) int c(int);
void f(int n) {
  (void)sizeof((w(1), 1)); __typeof__((w(1), 1)) y = 0; (void)_Alignof(__typeof__((w(1), 1))); (void)y;
  (void)_Generic(n, int: 0, default: (w(1), 1)); (void)_Generic((w(1), n), int: 0);
  (void)sizeof((n ? w(1) : w(2), 1));
  (void)sizeof(({ n ? w(1) : w(2); 1; }));
  (void)(0 && (c(1), 1)); (void)(1 || (n, c(1)));
  ({ return; n ? w(1) : w(2); });
}
static int g(int n) { return 0 ? (c(n), 1) : 0; }
int h(int n) { return g(n); }
