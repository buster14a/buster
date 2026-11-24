struct P { int a; };
void f(int c) {
  int x; x = 1;
  int y = 2; y += 3;
  int z = 0; z++;
  int w = 0; w = w + 1;
  int v; int u;
  { int k; k = 2; }
  static int s; s = 4;
  struct P p; p.a = 1;
  struct P q; q = (struct P){0};
  int *ptr; ptr = 0;
  int arr[2]; arr[0] = 1;
  volatile int vol; vol = 1;
  volatile int v2 = 0; v2 += 1;
  int m; if ((m = c)) {}
  int n; n = c, n = 2;
  int t; t = 0, (void)0;
  int a; a = 1; (void)sizeof(a);
  int b; (b) = 1;
  int o; o = 1; __typeof__(o) o2 = 0; (void)o2;
  int r; r = 1; (void)_Generic(r, int: 1);
  int l, l2; l = l2 = 0;
  int g; g = 1; ({ g = 2; });
  int e __attribute__((unused)); e = 1;
}
