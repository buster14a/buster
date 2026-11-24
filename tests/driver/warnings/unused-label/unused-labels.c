void f(int x) { l: x++; }
void g(int x) { a: b: x++; goto b; }
void h(void) { __label__ m; m: ; }
int k(int x) { void *p = &&q; q: return x + (p != 0); }
void n(int x) { __attribute__((unused)) u: x++; v: __attribute__((unused)); [[maybe_unused]] w: ; }
void o(int x) { if (x) { z: ; } }
#define L(n) n: ;
void q(void) { L(w) }
int r(int x) { return ({ y: x; }); }
void s(void) { l: ; { __label__ l; l: ; goto l; } }
void t(int x) { switch (x) { case 1: l: ; } }
