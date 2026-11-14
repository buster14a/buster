typeof(int (*)(struct Z *)) g;
struct Z *z;
int f(void) { return g(z); }
