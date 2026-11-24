__attribute__((const)) int c(int);
__attribute__((pure)) int p(int);
__attribute__((warn_unused_result)) int w(int);
#define CALLC(x) c(x)
#define CALLP(x) p(x)
#define CALLW(x) w(x)
#define ID(x) x
#define STATEMENT(x) c(x);
void f(int n) {
  CALLC(n); CALLP(n); CALLW(n); STATEMENT(n)
  c(ID(n)); w(ID(n));
}
