[[gnu::aligned(sizeof(int (*)(__attribute__((unused)) struct Z *)))]] int a;
int (*g)(struct Z *);
struct Z *z;
int f(void) { return g(z) + a; }
