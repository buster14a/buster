int f(void) __attribute__((overloadable));
int f(int) __attribute__((overloadable));
void *q = &f;
void *r = f;
int g(void) { return f != 0; }
