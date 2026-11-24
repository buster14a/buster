int g(void) { return 0; }
int h(void) { return 1; }
int h(void) __attribute__((alias("g")));
