int g(void) { return 0; }
void f(void) { int h(void) __attribute__((alias("g"))); }
int h(void) { return 1; }
