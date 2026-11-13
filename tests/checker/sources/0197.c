int f(struct S { int x; } s) {
 struct S { int y; } t; return t.y + s.x; }
