void f(int c) { int d; for (d = 0; c; d++) {} int e; for (e = 0; e < c; e++) {} }
void g(int c) { int f2; if (c) f2 = 1; else f2 = 2; int g2; g2 = 1; { int g2; g2 = 2; } }
void h(int c) { for (int i = 0; c; i++) { int z; z = 2; } }
