typeof(int (*)(int (*)(struct Z *), struct Z *)) g;
int h(struct Z *);
int f(void) { return g(h, 0); }
