// #2094: code generation's target-feature error at -c shows its source line and caret.
static inline __attribute__((always_inline, target("arch=haswell"))) int f(void) { return 1; }
int g(void) { return f(); }
