__attribute__((const)) int c(int);
__attribute__((warn_unused_result)) int w(int);
#define C c
#define W w
#define PICK(n) ((n) ? c(n) : c(n))
#define COMMA(n) ((void)0, c(n))
#define CO ,
#define N n
#define Q ?
void f(int n) {
  n ? C(n) : C(n); n, C(n); C(n), n; C(n);
  PICK(n); COMMA(n); n CO c(n); N ? c(n) : c(n); n Q c(n) : c(n);
  n ? W(n) : W(n); N ? w(n) : w(n);
}
