__attribute__((address_space(1))) int ***deep;
__attribute__((address_space(1))) int **mid;
__attribute__((address_space(1))) int **ai;
double **dp;
struct S { int x; } **sp;
int **ip;
void take(__attribute__((address_space(2))) int **p);
void f(void) {
  deep = mid;
  ai = dp;
  ai = sp;
  deep = ip;
  take(ai);
}
