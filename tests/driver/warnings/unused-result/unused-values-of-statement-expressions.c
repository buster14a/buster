void *p;
int e(int);
void f(int n, int a) {
  n ? ({ int z = 1; (void)z; }) : ({ int y = 2; (void)y; });
  n ? ({ e(n); }) : ({ 3; });
  n ? ({ (void)0; }) : ({ (void)1; });
  ({ int q = 1; (void)q; }), a;
  a, ({ int q = 1; (void)q; });
  n ? ({ *p; }) : ({ *p; });
}
