int x = 1 < 2 < 3;
int a(int p, int q, int r) { return p < q < r; }
int b(int p, int q, int r) { return p <= q > r; }
int c(int p, int q, int r) { return p == q == r || p < q == r || (p < q) < r || p == q < r || p != q != r; }
int g(int p, int q, int r, int s) { return p < q < r < s; }
