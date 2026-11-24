void f(int x) { int a; l: x++; { int b; c: ; } int d; m: ; int e; }
void g(int x) { int a; l: { int b; } (void)x; }
void h(int x) { l: { int b; k: ; } int y = 1 << 40; (void)x; }
void s(int x) { lab: ; static int st; (void)x; }
