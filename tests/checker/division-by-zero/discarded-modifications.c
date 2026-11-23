int a[2];
int h1(int y) { return (y = 0, 0) ? 1 << 40 : 2; }
int h2(int y) { char c = (y = 0, 1) ? 2 : 300; return c; }
int h3(int y) { return ({ y = 1; 0; }) ? a[5] : 2; }
int h4(int y) { return 0 && (y = 1) ? 1 << 40 : 2; }
int h5(int y) { return (0 ? (y = 1) : 0) ? 1 << 40 : 2; }
int h6(int y) { return ((void)(y = 1), 0) ? 1 << 40 : 2; }
int h7(int y) { if ((y = 1, 0)) return 1 / 0; return 2; }
int h8(int y) { return (y = 1) || 1 ? 2 : 1 / 0; }
int h9(int y) { return 1 || (y = 1) ? 2 : 1 / 0; }
int h10(int y) { return ((y = 1) ? 0 : 0) ? 1 / 0 : 2; }
int h11(int y) { return y++ ? 1 / 0 : 2; }
int h12(volatile int y) { return (y = 0) ? 1 / 0 : 2; }
int h13(int y) { int z = (y = 0) ? 1 / 0 : 2; return z; }
void h14(int y) { (y = 0) ? (void)(1 / 0) : (void)0; }
int h15(int y) { switch (y = 2) { case 1: return 1 / 0; } return 0; }
