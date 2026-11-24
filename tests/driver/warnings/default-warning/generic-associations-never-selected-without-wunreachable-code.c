void g(void) { int x; int b[2]; int c = _Generic(b, int[2]: 1, const int: 3, default: 2); (void)c; (void)x; }
