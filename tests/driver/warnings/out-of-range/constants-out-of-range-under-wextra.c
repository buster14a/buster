int f(signed char s, unsigned u) { return (s == 4000000000u) + (u < -1) + (s < u); }
int g(unsigned char c, signed char s, unsigned u, int i, int h(int, int)) {
  int r = (c == -1) + (c != 256) + ((c < 300) == (c > -5));
  r += (c < 300) ?: 1; r += h(c < 300, c > -5); r = c > 300 ? c < 400 : c < 500;
  r += (i < u) + (s < u);
  return r;
}
