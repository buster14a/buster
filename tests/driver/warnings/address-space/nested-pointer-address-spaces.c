__attribute__((address_space(1))) int **pp;
__attribute__((address_space(2))) int **qq;
__attribute__((address_space(1))) int **pp2;
const int **cp;
int **plain;
void f(void) {
  pp = qq;
  plain = pp;
  pp = plain;
  pp = pp2;
  cp = plain;
}
