// #2990's shapes: clang's CFG builder decides each condition by its heuristics (tryEvaluateBool), so the runtime warning
// in the branch it prunes and the -Wreturn-type fall-off past a condition it takes as true are not given. The last three
// are controls: parentheses and a `!` at the top stop the heuristics.
int ty[2];
int a(int x) { int w = 0; if (x * 0) w += ty[3]; return w; }
int b(int x) { int w = 0; if (x & 0) w += ty[3]; return w; }
int c(int x) { int w = 0; if ((x & 8) == 4) w += ty[3]; return w; }
int d(int x) { int w = 0; if ((x | 1) == 0) w += ty[3]; return w; }
int e(int x) { int w = 0; if (2 < (x == 1)) w += ty[3]; return w; }
int f(int x, int y) { int w = 0; if ((x < y) == 10) w += ty[3]; return w; }
int g(int x) { int w = 0; if (x == 1 && x == 2) w += ty[3]; return w; }
int h(int x) { int w = 0; if (x && !x) w += ty[3]; return w; }
int i(int x) { int w = 0; if (x * 0 || 0) w += ty[3]; return w; }
int j(int x) { int w = 0; while (x & 0) w += ty[3]; return w; }
int k(int x) { return x * 0 ? ty[3] : ty[0]; }
int l(int x) { return (x * 0 && ty[3]) + (x | 1 || ty[3]); }
int m(int x) { if (x | 1) return 1; }
int n(int x) { if (x < 5 || x > 3) return 1; }
int o(int x) { if (x || !x) return 1; }
int p(int x) { int w = 0; if ((x * 0)) w += ty[3]; return w; }
int q(int x) { return (x * 0) ? ty[3] : ty[0]; }
int r(int x) { if (!(x & 0)) return 1; }
