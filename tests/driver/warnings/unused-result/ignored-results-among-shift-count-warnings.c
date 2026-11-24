__attribute__((warn_unused_result)) int w(int);
void f(int x, int n, int a) {
  x << 40, w(1); n ? x << 40 : x >> -1; (x << 33) && a; a || (x >> 40); (w(x << 40), 0);
  _Generic(n, int: x, default: x << 40); n ? w(x << 33) : w(x >> 33);
}
