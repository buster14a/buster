struct A { int x; };
_Atomic(struct A) a;
_Atomic(struct A) *p;
int f(int n) { if (0) return a.x; if (n) return p->x; return 0; a.x = 1; }
int g(void) { return 0 && a.x; }
int h(void) { return 1 ? 0 : p->x; }
