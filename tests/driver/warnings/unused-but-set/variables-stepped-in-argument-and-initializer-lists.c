int g(int, int);
int h(int);
void f(void) { int c = 0; g(c++, 1); int d = 0; g(1, d++); int e = 0; h(e = 1); int k = 0; int a[2] = { k++, 1 }; (void)a;
  int m = 0; (void)(m++, 1); }
