int a[sizeof(struct V { int x; })];
struct V v;
int f(void) { int b[sizeof(struct W { int y; })]; struct W w = { 1 }; return w.y + (int)sizeof(b) + v.x; }
