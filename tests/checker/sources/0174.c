[[gnu::aligned(sizeof(int (*)(register struct Z *)))]] int a;
int (*g)(struct Z *);
struct Z *z;
int f(void) { return g(z) + a; }
