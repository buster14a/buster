// Assignments and conversions (#2376): the expression assigned to, a register variable's operand without its
// parentheses, and a conversion's source expression, with its parentheses.
int a(void) { 1 = 2; return 0; }
int b[2];
int c(int d[2]) { b = d; return 0; }
int *e(register int x) { return &x; }
int *f(void) { register int y; return &(y); }
void take(int *p);
int g[3];
int *h(void) { int x = g; int *p = 5; p = (7); take(9); return 1 + 2; }
int i(void) { return g; }
int j(int *q) { q = 1.5f; return 0; }
struct S { int m; } s;
int k(void) { int y = s; return 0; }
