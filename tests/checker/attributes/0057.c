int g(void) { return 0; }
int h(void) __attribute__((alias("g")));
int h(void) __attribute__((alias("g")));
