void f(int x) {
(void)_Generic(x, int: 1, char: 2, char: 3, int: 4);
(void)_Generic(x, const int: 1, int const: 2, int[2]: 3, default: 0);
(void)_Generic(x, int[2]: 1, int[3]: 2);
(void)_Generic(x, int: 1, const char: 2, char: 3, int: 4, long[1]: 5);
}
int h[_Generic(1, long: 1, const int: 2, long: 3)];
