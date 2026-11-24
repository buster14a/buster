typedef int I; typedef const int CI;
void f(int x) {
(void)_Generic(x, int: 1, int: 2, int: 3); (void)_Generic(x, I: 1, int: 2); (void)_Generic(x, char: 1, int: 2, char: 3, short: 4);
(void)_Generic(x, int: 1, int: 2, default: 1, default: 2, default: 3); (void)_Generic(x, default: 1, default: 2);
(void)_Generic(x, CI: 3, default: 0); (void)_Generic(x, int*: 1, int*: 2);
(void)_Generic(x, int(*)[]: 1, int(*)[2]: 2); (void)_Generic(x, long: 1, long int: 2); (void)_Generic(x, default: 0, int[2]: 3, int: 1, int: 2);
}
int g = _Generic(1, int: 1, int: 2);
int h[_Generic(1, long: 1, long: 2)];
int k(void) { switch (1) { case _Generic(1, int: 1, int: 2): return 0; } return 1; }
_Static_assert(_Generic(1, default: 1, default: 2), "");
