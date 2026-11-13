int f(int a[sizeof(struct V { int x; })], struct V *v) { struct V *w = v; return w->x; }
