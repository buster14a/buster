// Which conditions clang's heuristics decide, and which they leave to its evaluator (#2990): a literal with an implicit
// conversion, or inside parentheses past a conversion, is no literal to them; a `|` of two literals, or of none, decides
// nothing of its own; comparisons of two different operands, or of constants of different widths or signedness, or of an
// enumerator and a literal, aren't overlaps; and a do statement's condition is read whole.
int ty[2];
enum E { E0, E1 };
enum F { F0, F1 };
struct S { int i; char c; _Bool b; } s, *ps;
int v[4];
int f1(int x) { int w = 0; if ((x & 8L) == 4) w += ty[3]; return w; }
int f2(int x) { int w = 0; if ((x & 8) == 4L) w += ty[3]; return w; }
int f3(long x) { int w = 0; if ((x & 8) == 4) w += ty[3]; return w; }
int f4(int x) { int w = 0; if ((x & -1) == 4) w += ty[3]; return w; }
int f5(int x) { int w = 0; if ((x & 8) == -4) w += ty[3]; return w; }
int f6(int x) { int w = 0; if ((x & 8u) == 4u) w += ty[3]; return w; }
int f7(char c) { int w = 0; if ((c & 8) == 4) w += ty[3]; return w; }
int f8(int x) { int w = 0; if (x * 0.0) w += ty[3]; return w; }
int f9(int x) { int w = 0; if (x * (1 - 1)) w += ty[3]; return w; }
int f10(_Bool b) { int w = 0; if (b == 2) w += ty[3]; return w; }
int f11(int x) { int w = 0; if ((unsigned)(x == 1) < 2) w += ty[3]; return w; }
int f12(int x, int y) { int w = 0; if (((x == 1) | (y == 2)) == 0) w += ty[3]; if (((x == 1) | 2) == 4) w += ty[3]; return w; }
int f13(int x, int y) { int w = 0; if (((x == 1) + (y == 2)) == 3) w += ty[3]; return w; }
int f14(int x, int y) { int w = 0; if (x == 1 && y == 2) w += ty[3]; return w; }
int f15(int x) { int w = 0; if (x == 1 && x == 2L) w += ty[3]; return w; }
int f16(char c) { int w = 0; if (c == 1 && c == 'a') w += ty[3]; return w; }
int f17(unsigned u) { int w = 0; if (u > 5 && u < 3) w += ty[3]; return w; }
int f18(int x) { int w = 0; if (E0 == x && E1 == x) w += ty[3]; return w; }
int f19(int x) { int w = 0; if (x == E0 && x == 2) w += ty[3]; return w; }
int f20(int x) { int w = 0; if (x == E0 && x == F1) w += ty[3]; return w; }
int f21(double d) { int w = 0; if (d < 1.0 && d > 2.0) w += ty[3]; return w; }
int f22(float f) { int w = 0; if (f == 1 && f == 2) w += ty[3]; return w; }
int f23(void) { int w = 0; if (s.c && !s.c) w += ty[3]; return w; }
int f24(void) { int w = 0; if (s.i && !s.i) w += ty[3]; return w; }
int f25(void) { int w = 0; if (s.b && !s.b) w += ty[3]; return w; }
int f26(int* p) { int w = 0; if (p && !p) w += ty[3]; return w; }
int f27(int i) { int w = 0; if (v[i] && !v[i]) w += ty[3]; return w; }
int f28(void) { int w = 0; if (ps->i == 1 && ps->i == 2) w += ty[3]; return w; }
int f29(void) { int w = 0; if (v[1] == 1 && v[2] == 2) w += ty[3]; return w; }
int f30(int x, int y) { int w = 0; if ((x * 0) && y) w += ty[3]; return w; }
int f31(int x, int y) { int w = 0; if (y && (x * 0)) w += ty[3]; return w; }
int f32(int x, int y) { int w = 0; if (y || (x | 1)) return w; return ty[3]; }
int f33(int x) { int w = 0; if (x | (long)1) return w; return ty[3]; }
int f34(int x) { int w = 0; if (x | (1)) return w; return ty[3]; }
int f35(int x) { int w = 0; if (x | -(1)) return w; return ty[3]; }
int f36(int x) { int w = 0; if (1 | 2) return w; return ty[3]; }
int f37(int x, int y) { int w = 0; if (x | y) return w; return ty[3]; }
int f38(int x) { int w = 0; if (x | E1) return w; return ty[3]; }
int f39(int x) { int w = 0; if (x | E0) return w; return ty[3]; }
int f40(int x) { int w = 0; if (x | 'a') return w; return ty[3]; }
int f41(int x) { int w = 0; if (-(x * 0)) w += ty[3]; return w; }
int f42(int x, int y) { int w = 0; if ((x == 1 && x == 2) || y) w += ty[3]; return w; }
int f43(int x, int y) { int w = 0; if (x == 1 && (x == 2 || y)) w += ty[3]; return w; }
int f44(int x, int y) { int w = 0; do w++; while ((x * 0) && y); return w + ty[3]; }
int f45(int x) { int w = 0; do { if (x == 1 && x == 2) w += ty[3]; } while (x == 1 && x == 2); return w; }
int f46(int x) { int w = 0; for (; x & 0;) w += ty[3]; return w; }
int f47(int x) { return x ?: (x * 0); }
int f48(int x, int y) { return (x | 1) ? y : ty[3]; }
int f49(int x) { return x * 0 && x * 0 ? ty[3] : 0; }
