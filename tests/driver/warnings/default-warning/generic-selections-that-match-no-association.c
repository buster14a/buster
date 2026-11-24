typedef int *P; typedef const int CI;
void f(P q, CI c, int a[2], int *p, int x) { (void)_Generic(q, char: 1); (void)_Generic(c, char: 1); (void)_Generic(a, char: 1);
(void)_Generic((P)0, char: 1); (void)_Generic(x + 1.0, char: 1); (void)_Generic((p), char: 1, short: 2); int y = _Generic(p, char: 1) + 1; (void)y; }
int g = _Generic(1.0, char: 1);
