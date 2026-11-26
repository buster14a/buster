int f(void);
int g(int x, const char *p) {
  int r = x == x;
  r += p == "x";
  if (f) r++;
  r += f == 0;
  return r;
}
