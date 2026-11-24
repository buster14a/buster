__attribute__((warn_unused_result)) int w(int);
__attribute__((warn_unused_result)) long wl(int);
__attribute__((const)) int c(int);
int e(int);
void f(void);
void g(void);
struct S { int a; } s;
void h(int n) {
  n ? (f(), 0) : (g(), 1); n ? (f(), 1) : (g(), (0)); n ? (n ? (f(), 0) : (g(), 0)) : (g(), 0);
  (n ? (f(), 0) : (g(), 0)), n; (n ? c(n) : c(n)), 0; 0, (n ? c(n) : e(n));
  n ? (0, w(n)) : (n, (0)); n ? (n, (n, w(n))) : w(n); n ? (n ? 1 : 2) : (n ? 3 : f());
  n, (n, w(n)); (n, w(n)), n; n, (n ? (f(), 0) : (g(), 0));
  n ?: w(n); e(n) ?: 0; w(n) ?: w(n);
  n ? wl(n) : w(n); n ? w(n) : e(n); n ? w(n) : n + 1; n ? n + 1 : w(n); n ? c(n) : (n, 0); n ? s.a : s.a;
  _Generic(n, int: (n ? w(n) : w(n))); _Generic(n, int: (f(), 0)); _Generic(n, int: (n ? 1 : 2)); n ? _Generic(n, int: w(n)) : c(n);
  for (n ? w(n) : w(n); n; n ? c(n) : c(n)) { }
}
