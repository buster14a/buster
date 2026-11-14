int f(struct T *p) { return p != 0; }
struct T *t;
int g(void) { return f(t); }
