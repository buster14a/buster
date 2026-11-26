static void *buf[5];
__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }
long double run(long double x) { long double r = x + __builtin_setjmp(buf); if (r == x) jump(); return r; }
