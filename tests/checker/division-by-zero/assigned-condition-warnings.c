int a[2];
int g1(int y, int i) { return (y = 0) ? i++ + i++ : 2; }
int g2(int y) { return (y = 1) ? a[5] : 2; }
int g3(int y) { if (y = 1) return 1; }
int g4(int y) { while (y = 1) { } }
int g6(int y) { return ({ y = 0; }) ? a[7] : 2; }
int g7(int y) { return (y = 0, 0) ? a[7] : 2; }
int g9(int y) { return ++y ? 2 : 1 / 0; }
int g10(int y) { return (y = 3) ? 2 : (y = 0) ? 1 / 0 : 4; }
int g11(int y) { return __builtin_expect(y = 0, 0) ? 1 / 0 : 2; }
int g12(int y) { do { } while (y = 0); return 1 / 0; }
int g13(int *p) { return (*p = 0) ? 1 / 0 : 2; }
int g14(int y) { return (y = 0) ? 1 << 40 : 2; }
int g15(int y) { char c = (y = 0) ? 300 : 2; return c; }
