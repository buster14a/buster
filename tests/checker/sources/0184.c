int f(struct S { int x; } a, struct S { int y; } b, struct S *c) { return a.x + b.y + c->x; }
