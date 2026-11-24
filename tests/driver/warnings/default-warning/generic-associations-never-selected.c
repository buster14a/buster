typedef const int CI;
void g(void) { int x; int b[2];
int c = _Generic(b, int[2]: 1, default: 2);
int d = _Generic(x, _Atomic int: 1, default: 2);
int e = _Generic(x, int const: 1, volatile long: 3, default: 2);
int f = _Generic(x, CI: 1, default: 2);
int h = _Generic(&x, int * __attribute__((address_space(1))): 1, int * restrict: 4, default: 2);
int i = sizeof(_Generic(x, const char: 1, default: 2));
(void)c; (void)d; (void)e; (void)f; (void)h; (void)i; }
int k = _Generic(1, const int: 1, int[3]: 2, default: 0);
