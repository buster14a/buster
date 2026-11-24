int a[2];
int f(int n) {
  if (n && 0) { n = n / 0; }
  if (!(n || 1)) { n = a[5]; }
  if ((long)(n || 1)) { n += 1; } else { n = n % 0; }
  while (n && 0) { n = *(int *)0; }
  if (n && (n || 1)) { n = n / 0; }
  return n;
}
int g(int n) {
  if (n || 1) return 1;
}
int h(int n) {
  while (n || 1) { if (n) return 2; }
}
int k(int n) {
  if (n && 1) return 1;
}
