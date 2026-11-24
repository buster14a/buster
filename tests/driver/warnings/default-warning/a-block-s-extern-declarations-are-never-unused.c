void f(void) { extern int x; }
void g(void) { extern int y; (void)y; }
void h(void) { extern int z(void); }
void k(void) { extern int w __attribute__((unused)); }
void m(void) { extern int v; { extern int v; v = 1; } }
int q;
void n(void) { extern int q; }
static int s5;
void o(void) { extern int s5; (void)s5; }
void p(void) { extern int arr[]; extern int a, b; (void)b; }
void s(void) { static int st; register int r; int loc; }
void t(void) { int y4; { extern int y4; } }
