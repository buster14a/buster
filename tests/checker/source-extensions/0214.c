struct O { char c; struct I { long x; } i; unsigned b:3; }; _Static_assert(sizeof(struct O)==24, "layout"); int f(struct O *p) { return p->i.x; }
