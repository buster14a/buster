struct s; union u; enum e; typedef struct s S;
void f(struct s *p, struct s *q, union u *w, enum e *n, S *t) {
  (void)(p + 1); (void)(1 + p); (void)(p - q); p++; --p; p += 2; (void)p[1]; (void)1[p];
  (void)(w + 1); (void)(n - 1); (void)(t + 1);
}
