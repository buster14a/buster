__attribute__((warn_unused_result)) int w(int);
__attribute__((const)) int c(int);
int e(int);
enum E { A, B };
enum E g(void);
enum __attribute__((warn_unused_result)) F { C };
enum F h(void);
double sin(double);
double rint(double);
int (*fp)(int);
void f(int n, int a, int b, int x, double d, volatile int *p) {
  n ? (a && w(n)) : (b || w(n)); n ? (a && e(n)) : (b || w(n)); (x && 0), w(n); (x && w(n)), 0; x, (a && b);
  (a && b), x; a && (n ? w(n) : w(n)); a && (n ? w(n) : e(n)); a || (x, (b && c(n))); (a && b) || (n ? a : b);
  a && ({ e(n); }); n ? ({ 1; }) : ({ 2; }); ({ e(n); 3; }), x;
  a && g(); a && h(); a && sin(d); a && rint(d); a && fp(1);
  *p; a ? *p : *p; *p, a; a, *p; a && *p;
}
