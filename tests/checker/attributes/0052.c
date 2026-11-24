int g(void) { return 0; }
__attribute__((alias("g"))) int h(void) { return 1; }
