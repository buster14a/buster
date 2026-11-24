typedef const int CI;
struct s { int a; };
void f(const int p, const struct s *q) {
  const int x = 1; x = 2; x++; p = 3;
  const int a[2] = {0}; a[0] = 1; const int m[2][2] = {0}; m[0][1] = 1;
  CI y = 1; y = 2; const struct s v = {0}; v.a = 1; q->a = 2;
}
void g(const int *r) { r[0] = 1; *r = 2; }
