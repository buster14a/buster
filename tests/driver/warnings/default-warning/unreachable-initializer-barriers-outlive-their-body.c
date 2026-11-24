int f(int n) {
  return n;
  int dead = n;
  return dead;
}
int g(int n) {
  int a = n + 1;
  if (n > 1) n = a;
  int b = n + 2;
  if (n > 2) n = b;
  int c = n + 3;
  if (n > 3) n = c;
  int d = n + 4;
  if (n > 4) n = d;
  return n;
}
