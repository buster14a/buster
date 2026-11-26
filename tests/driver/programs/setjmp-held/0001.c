static void *buf[5];
__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }
int run(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{ int r = a + (b * (c - (d + (e * (f - (g + (h * (i - (j + __builtin_setjmp(buf)))))))))); if (r == 0) jump(); return r; }
