int abs(int);
double rint(double);
void f(int n, double d) {
  abs(n); rint(d); __builtin_rint(d);
}
