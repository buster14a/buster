static void *buf[5];
__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }
long double run(long double x) { if (__builtin_setjmp(buf) == 0) { x = x * 2; jump(); } return x; }
