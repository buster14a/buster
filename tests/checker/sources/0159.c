int f(int a[sizeof(struct V *)], typeof(struct W *) b, struct V *c) { struct V *v = c; struct W *w = b; return (v != 0) + (w != 0) + a[0]; }
