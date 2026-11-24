int a[2]; int fn(int);
struct P { int x, y; };
void f(int i) {
  int x1[2] = { a[5], 0 };
  int x2[2] = { (a[5]), 0 };
  int x3[2] = { -a[5], 0 };
  int x4[2] = { fn(a[5]), 0 };
  int x5[2] = { a[5] ? 1 : 2, 0 };
  int x6[2] = { (char)a[5], 0 };
  int *x7[2] = { &a[5], 0 };
  int x8[2] = { [1] = a[5] };
  int x9[1][1] = { { a[5] } };
  struct P x10 = { a[5], i };
  int x11 = { a[5] };
  int *x12 = (int[]){ a[5] };
  int x13[2] = { a[i ? 5 : 6] };
  int x14[2] = { a[5] , a[6] };
  int x15[2] = { i, a[5] = 1 };
  int x16[2] = { (i, a[5]) };
  static int s = 0; (void)s;
  (void)x1;(void)x2;(void)x3;(void)x4;(void)x5;(void)x6;(void)x7;(void)x8;(void)x9;(void)x10;(void)x11;(void)x12;(void)x13;(void)x14;(void)x15;(void)x16;
}
int g1[2] = { 0 };
