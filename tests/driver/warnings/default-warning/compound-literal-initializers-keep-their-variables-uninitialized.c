int reads(void) {
  int u;
  int e = (int){ u };
  return e;
}
int self(void) {
  int x = (int){ x };
  return x;
}
int nested(void) {
  int u;
  int v;
  int e = (int){ (int){ u } + v };
  return e;
}
int assigned(void) {
  int x;
  x = (int){ x };
  return x;
}
int braced(void) {
  int y = { (int){ y } };
  return y;
}
int pointer(void) {
  int *p = (int *){ p };
  return p != 0;
}
