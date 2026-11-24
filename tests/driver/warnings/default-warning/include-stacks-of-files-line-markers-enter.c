# 1 "lm_main.c"
# 1 "outer.h" 1
# 1 "inner.h" 1
int f(void) { return 1 << 40; }
int f2(void) { return 1 << 41; }
# 2 "outer.h" 2
int g(void) { return 1 << 33; }
# 3 "lm_main.c" 2
int h(void) { return 1 << 34; }
# 1 "again.h" 1 3 4
int k(void) { return 1 << 35; }
# 4 "lm_main.c" 2
# 1 "user.h" 1
int m(void) { return 1 << 36; }
# 5 "lm_main.c" 2
#error done
