struct S { int x; int y; };
struct T { int n; int *p; };
int *gp = &(int){ 4 };
struct S *gs = &(struct S){ .x = 3 };
int *gq = (int[]){ 5, 6 };
int scalars(void) {
  int e = (int){ 4 };
  long l;
  l = (long){ 5 };
  int *q;
  q = (int *){ 0 };
  return e + (int)l + (q != 0) + *gp + gs->x + gq[1];
}
int aggregates(void) {
  int a[2] = { (int){ 1 }, (int){ 2 } };
  int z = { (int){ 3 } };
  struct S s = (struct S){ .x = 1 };
  struct S m = { .x = (int){ 1 } };
  int *p = (int[2]){ 1, 2 };
  s = (struct S){ .y = 2 };
  return a[0] + z + s.x + m.x + p[1];
}
int loops(int n) {
  int total = 0;
  for (int i = 0; i < n; i += 1) { int e = (int){ i }; total += e + (int){ 2 }; }
  while (n > 0) { int d = (int){ n }; total += d; n -= 1; }
  return total;
}
int addresses(void) {
  int *p = &(int){ 4 };
  int **pp = &(int *){ &(int){ 5 } };
  return *p + **pp;
}
int nested(void) {
  int e = (int){ (int){ 4 } };
  struct T t = (struct T){ .n = (int){ 1 }, .p = &(int){ 2 } };
  int a = (int){ (int){ (int){ 3 } } + 1 };
  return e + t.n + *t.p + a;
}
