__attribute__((const)) int c(int);
__attribute__((warn_unused_result)) int w(int);
void f(int n) {
  (void)sizeof(({ c(n); w(n); 1; }));
  int a[n];
  (void)sizeof(a[c(n)]);
  ({ c(n); 1; });
}
