void f(int n) {
  int a[n] = {1};
  int b[n + 1][2] = {{1}};
  char s[n] = "ab";
  int c[(n)] = {1, 2}, d = q1;
  int e[n] = 5;
  int use = a[0] + b[0][0] + s[0] + c[0] + e[0] + d;
  int (*p)[n] = 0;
  char *bad = use;
  int g[n] = {1, "x"}; char *t3 = g;
  int h[n] = {}; char *t4 = h;
  (void)p; (void)bad; (void)t3; (void)t4;
}
int x = y;
