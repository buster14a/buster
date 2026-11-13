int g(struct S { int y; } *p, int a[sizeof(void (*)(struct S { int x; }))]) { return p->y; }
