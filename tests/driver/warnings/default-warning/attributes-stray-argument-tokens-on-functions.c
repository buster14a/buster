void f(void) __attribute__((unused)) __attribute__((aligned(8 8))) { } int kept; int *p = &kept; int y __attribute__((aligned(8 8))), z;
int (*q)(void) __attribute__((aligned(8 8)));
int r(void) __asm__("r") __attribute__((aligned(8 8)));
int s, t(void) __attribute__((aligned(8 8)));
